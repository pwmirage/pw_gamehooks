/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

#include <stdio.h>
#include <windows.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <wchar.h>
#include <inttypes.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <stdbool.h>
#include <assert.h>
#include <wingdi.h>

#include "pw_api.h"
#include "common.h"
#include "d3d.h"
#include "csh.h"
#include "avl.h"
#include "pw_item_desc.h"
#include "extlib.h"
#include "idmap.h"

#ifndef ENOSPC
#define	ENOSPC		28	/* No space left on device */
#endif

extern unsigned g_target_dialog_pos_y;

static int g_profile_id = -1;
static char g_profile_idstr[32];
bool g_replace_font = true;
static wchar_t g_version[32];
static wchar_t g_build[32];

static struct pw_idmap *g_elements_map;

static wchar_t g_win_title[128];
static bool g_reload_title;

DWORD __stdcall
reload_title_cb(void *arg)
{
	SetWindowTextW(g_window, g_win_title);
}

static bool __thiscall
hooked_on_game_enter(struct game_data *game, int world_id, float *player_pos)
{
	bool ret = pw_on_game_enter(game, world_id, player_pos);

	/* we don't have player info yet, so defer changing the title */
	g_reload_title = true;
	return ret;
}

static bool __fastcall
hooked_on_game_leave(void)
{
	DWORD tid;

	pw_on_game_leave();

	snwprintf(g_win_title, sizeof(g_win_title) / sizeof(g_win_title[0]), L"PW Mirage");
	/* the process hangs if we update the title from this thread... */
	CreateThread(NULL, 0, reload_title_cb, NULL, 0, &tid);
}

static bool g_ignore_next_craft_change = false;

void settings_on_ui_change(const char *ctrl_name, struct ui_dialog *dialog);

/* button clicks / slider changes / etc */
static unsigned __stdcall
on_ui_change(const char *ctrl_name, struct ui_dialog *dialog)
{
	unsigned __stdcall (*real_fn)(const char *, void *) = (void *)0x6c9670;

	pw_debuglog(1, "ctrl: %s, win: %s\n", ctrl_name, dialog->name);

	if (strncmp(dialog->name, "Win_Setting", strlen("Win_Setting")) == 0 && strcmp(ctrl_name, "customsetting") == 0) {
		g_settings_show = true;
		return 1;
	}

	if (strcmp(dialog->name, "Win_Produce") == 0 && strncmp(ctrl_name, "set", 3) == 0) {
		g_ignore_next_craft_change = true;
	}

	unsigned ret = real_fn(ctrl_name, dialog);

	if (strcmp(dialog->name, "Win_SettingSystem") == 0) {
		settings_on_ui_change(ctrl_name, dialog);
	}

	return ret;
}

PATCH_JMP32(0x55006d, on_ui_change);

static bool g_in_dialog_layout_load = false;

static void __thiscall
hooked_on_dialog_show(struct ui_dialog *dialog, bool do_show, bool is_modal, bool is_active)
{
	char *n = dialog->name;
	if (do_show && is_active) {
		//pw_log("%s", n);
	}
	if (!g_in_dialog_layout_load && (
			strcmp(n, "WorldMap") == 0 ||
			strcmp(n, "GuildMap") == 0 ||
			strcmp(n, "WorldMapTravel") == 0)
			) {
		void *con = pw_get_dialog(g_pw_data->game->ui->ui_manager, "Dlg_Console");
		if (con) {
			pw_dialog_show(con, do_show, 0, 0);
			if (do_show) {
				pw_bring_dialog_to_front(g_pw_data->game->ui->ui_manager, con);
				pw_dialog_set_can_move(con, false);
			}
		}
	}
	pw_dialog_show(dialog, do_show, is_modal, is_active);

	if (!g_in_dialog_layout_load && do_show && strncmp(dialog->name, "Win_Quickbar", strlen("Win_Quickbar")) == 0) {
		pw_dialog_on_command(dialog, "new");
		pw_dialog_on_command(dialog, "new");

	}
}

static unsigned __thiscall
hooked_load_dialog_layout(void *ui_manager, void *unk)
{
	unsigned __thiscall (*real_fn)(void *ui_manager, void *unk) = (void *)0x6c8b90;
	unsigned ret;

	g_in_dialog_layout_load = true;
	ret = real_fn(ui_manager, unk);
	g_in_dialog_layout_load = false;
	return ret;
}

void
pw_ui_thread_sendmsg(mg_callback cb, void *arg1, void *arg2)
{
	struct thread_msg_ctx *ctx = malloc(sizeof(*ctx));
	assert(ctx != NULL);
	ctx->cb = cb;
	ctx->arg1 = arg1;
	ctx->arg2 = arg2;
	SendMessage(g_window, MG_CB_MSG, 0, (LPARAM)ctx);
}

void
pw_ui_thread_postmsg(mg_callback cb, void *arg1, void *arg2)
{
	struct thread_msg_ctx *ctx = malloc(sizeof(*ctx));
	assert(ctx != NULL);
	ctx->cb = cb;
	ctx->arg1 = arg1;
	ctx->arg2 = arg2;
	PostMessage(g_window, MG_CB_MSG, 0, (LPARAM)ctx);
}

struct ring_buffer_sp_sc *g_game_thr_queue;

int
pw_game_thr_post_msg(mg_callback fn, void *arg1, void *arg2)
{
	struct thread_msg_ctx *msg = calloc(1, sizeof(*msg));
	int rc;

	assert(msg != NULL);
	msg->cb = fn;
	msg->arg1 = arg1;
	msg->arg2 = arg2;

	rc = ring_buffer_sp_sc_push(g_game_thr_queue, msg);
	if (rc != 0) {
		free(msg);
	}

	return rc;
}

extern bool g_show_console;

static void
set_pw_version(void)
{
	FILE *fp = fopen("../patcher/version", "rb");
	int version;

	if (!fp) {
		return;
	}

	fseek(fp, 4, SEEK_SET);
	fread(&version, sizeof(version), 1, fp);
	fclose(fp);
	pw_log_color(0x11FF00, "PW Version: %d. Hook build date: %s\n", version, HOOK_BUILD_DATE);

	snwprintf(g_build, sizeof(g_build) / sizeof(wchar_t), L"");
	snwprintf(g_version, sizeof(g_version) / sizeof(wchar_t), L"%S", HOOK_BUILD_DATE);
	g_version[20] = 0;

	patch_mem_u32(0x563c6c, (uintptr_t)g_version);
	patch_mem_u32(0x563cb6, (uintptr_t)g_build);
}

static void __thiscall
hooked_add_chat_message(void *cecgamerun, const wchar_t *str, char channel, int player_id, const wchar_t *name, char unk, char emote)
{
	if (channel == 12) {
		pw_debuglog(1, "received (%d): %S", channel, str);
		if (wcscmp(str, L"update") == 0) {
			g_update_show = true;
		}

		return;
	} else if (channel == 13) {
		wchar_t msg[256];
		snwprintf(msg, sizeof(msg) / sizeof(msg[0]), L"(Discord) %s", str);
		pw_add_chat_message(cecgamerun, msg, 1, -1, L"Discord", unk, emote);
		return;
	}

	pw_add_chat_message(cecgamerun, str, channel, player_id, name, unk, emote);
}

static void parse_cmdline(void);

static unsigned __thiscall
hooked_pw_load_configs(struct game_data *game, void *unk1, int unk2)
{
	unsigned ret = pw_load_configs(game, unk1, unk2);
	DWORD tid;

	/* always enable ingame console (could have been disabled by the game at its init) */
	patch_mem(0x927cc8, "\x01", 1);

	if (g_reload_title) {
		g_reload_title = false;

		wchar_t *player_name = *(wchar_t **)(((char *)game->player) + 0x5cc);
		snwprintf(g_win_title, sizeof(g_win_title) / sizeof(g_win_title[0]), L"%s - PW Mirage", player_name);
		/* the process hangs if we update the title from this thread... */
		CreateThread(NULL, 0, reload_title_cb, NULL, 0, &tid);
	}

	return ret;
}

/* TODO tidy up */
int
parse_console_cmd(const char *in, char *out, size_t outlen)
{
	if (_strnicmp(in, "d ", 2) == 0) {
		_snprintf(out, outlen, "d_c2scmd %s", in + 2);
		return 0;
	} else if (_strnicmp(in, "di ", 3) == 0) {
		struct pw_idmap_el *node;
		unsigned pid = 0, id = 0;
		uint64_t lid;
		int count = 1;
		int rc;

		const char *hash_str = strstr(in, "#");
		if (hash_str) {
			in = hash_str + 1;
		} else {
			in = in + 3;
		}

		rc = sscanf(in, "%d : %d %d", &pid, &id, &count);
		if (rc >= 2 && g_elements_map) {
			lid = (pid > 0 ? 0x80000000 : 0) + 0x100000 * pid + id;
			_snprintf(out, outlen, "d_c2scmd 10800 %d %d", pw_idmap_get_mapping(g_elements_map, lid, 0), count);
		} else {
			_snprintf(out, outlen, "d_c2scmd 10800 %s", in);
		}
		return 0;
	} else {
		_snprintf(out, outlen, "%s", in);
		return -1;
	}
}

/* always try to get detailed item info on accquire */
static void __stdcall
hooked_pw_get_info_on_acquire(void *stack, unsigned need_info, unsigned char inv_id, unsigned char slot_id)
{
    unsigned *expire_time;

    if (need_info) {
        pw_get_item_info(inv_id, slot_id);
        return;
    }

    /* read the upper frame's stack */
    expire_time = (unsigned *)(stack + 0x1c);
    if (*expire_time == 0x631b) {
        /* hooked magic number, this item came from a task and the above time is not valid yet */
        *expire_time = 0;
        pw_get_item_info(inv_id, slot_id);
    }
}

PATCH_MEM(0x44cdae, 2, "nop; nop;");
PATCH_MEM(0x44cd41, 2, ".byte 0x1b; .byte 0x63");

TRAMPOLINE(0x44cdb6, 8,
	"push eax;"
	"push esp;"
	"call 0x%x",
	hooked_pw_get_info_on_acquire);

static bool g_exiting = false;
static bool g_unloading = false;
static float g_local_max_move_speed = 25.0f;

/* close the game on "exit" event instead of "IDCANCEL". This is paired up
 * with interfaces.pck change -> clicking the button will generate an "exit"
 * command. Pressing ESC always sends IDCANCEL and we don't want to close
 * on that. */
PATCH_MEM(0x55f919, 5, "push 0x%x", "exit");

static void
hooked_exit(void)
{
	g_unloading = true;
	g_exiting = true;
	g_replace_font = false;

	/* our hacks sometimes crash on exit, not sure why. they're hacks, so just ignore the errors */
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

static size_t
append_crash_info_cb(char *buf, size_t bufsize, void *parent_hwnd, void *ctx)
{
	size_t buf_off = 0;

	g_replace_font = false;
	*(HWND *)parent_hwnd = g_window;

	return buf_off;
}

static void
hooked_exception_handler(void)
{
	EXCEPTION_POINTERS *ExceptionInfo;

	__asm__(
		"mov %0, eax;"
		: "=r"(ExceptionInfo));

	handle_crash(ExceptionInfo);
}

static wchar_t *g_proc_type_name[] = {
	L"Doesn't drop on death",
	L"Unable to drop",
	L"Unable to sell",
	NULL,
	L"Unable to trade",
	NULL,
	L"Bound on equip",
	L"Unable to destroy",
	L"Disappears on map change",
	NULL,
	L"Disappears on death",
	NULL,
	L"Unrepairable"
};

static void __fastcall
hooked_item_add_ext_desc(void *item)
{
	struct pw_item_desc_entry *entry;
	uint32_t id = *(uint32_t *)(item + 8);
	uint32_t proc_type = *(uint32_t *)(item + 16);
	bool sep_printed = false;

	entry = pw_item_desc_get(id);
	if (entry && *(wchar_t *)entry->aux != 0) {
		if (!sep_printed) {
			pw_item_desc_add_wstr(item + 0x44, L"\r\r");
			//sep_printed = true;
		}
		pw_item_desc_add_wstr(item + 0x44, entry->aux);
	} else if (!entry) {
		pw_item_add_ext_desc(item);
	}

	if (proc_type) {
		for (int i = 0; i < sizeof(g_proc_type_name) / sizeof(g_proc_type_name[0]); i++) {
			if (!(proc_type & (1 << i)) || !g_proc_type_name[i]) {
				continue;
			}
			if (!sep_printed) {
				pw_item_desc_add_wstr(item + 0x44, L"\r");
				sep_printed = true;
			}
			pw_item_desc_add_wstr(item + 0x44, L"\r^00ffff");
			pw_item_desc_add_wstr(item + 0x44, g_proc_type_name[i]);
		}
	}
}

static HFONT WINAPI (*org_CreateFontIndirectExW)(ENUMLOGFONTEXDVW* lpelf);
static HFONT WINAPI hooked_CreateFontIndirectExW(ENUMLOGFONTEXDVW* lpelf)
{
	if (!g_replace_font) {
		/* no need to replace font in MessageBoxes, etc... */
		return org_CreateFontIndirectExW(lpelf);
	}

	LOGFONTW *lplf = &lpelf->elfEnumLogfontEx.elfLogFont;
	wcscpy(lplf->lfFaceName, L"Microsoft Sans Serif");
	return org_CreateFontIndirectExW(lpelf);
}


static bool __thiscall
hooked_translate3dpos2screen(void *viewport, float v3d[3], float v2d[3])
{
	bool ret = pw_translate3dpos2screen(viewport, v3d, v2d);
	/* the position is usually converted to int and it often twitches by 1px.
	 * adding 0.5 helps a ton even though it's not a perfect solution */
	v2d[0] += 0.5;
	v2d[1] += 0.5;
	return ret;
}

static void *
hooked_alloc_produced_item(uint32_t id, uint32_t expire_time, uint32_t count, uint32_t id_space)
{
	void *item = pw_alloc_item(id, expire_time, count, id_space);
	uint32_t class = *(uint32_t *)(item + 4);
	uint32_t equip_mask = *(uint32_t *)(item + 36);

	if (g_ignore_next_craft_change) {
		/* this recipe was chosen automatically on tab switch, so don't change the fashion */
		g_ignore_next_craft_change = false;
		return item;
	}

	if (class == 6) {
		/* fashion craft */
		char tmpbuf[16];

		void *preview_win = pw_get_dialog(g_pw_data->game->ui->ui_manager, "Win_FittingRoom");
		int equip_id = 0;

		for (int i = 13; i <= 16; i++) {
			if (equip_mask & (1 << i)) {
				equip_id = i;
				break;
			}
		}

		if (!equip_id) {
			/* fashion, but can't be previewed */
			return item;
		}

		bool was_shown = *(bool *)(preview_win + 0x6c);
		if (!was_shown) {
			pw_dialog_show(preview_win, true, false, false);
		}

		snprintf(tmpbuf, sizeof(tmpbuf), "Equip_%d", equip_id);
		void *equip_el = pw_dialog_get_el(preview_win, tmpbuf);
		pw_fashion_preview_set_item(preview_win, item, equip_el);
	}

	return item;
}

static int __cdecl
hooked_fseek(FILE *fp, int32_t off, int32_t mode)
{
	int64_t loff = off;

	if (off < -65536) {
		loff = *(uint32_t *)&off;
	}

	return _fseeki64(fp, loff, mode);
}

static void
item_desc_avl_wchar_fn(void *el, void *ctx1, void *ctx2)
{
	struct pw_avl_node *node = el;
	struct pw_item_desc_entry *entry = (void *)node->data;
	wchar_t *wstr;
	size_t i, max_chars = entry->len * 1 + 16;

	wstr = entry->aux = malloc(max_chars * sizeof(wchar_t));
	if (!wstr) {
		assert(false);
		return;
	}

	snwprintf((wchar_t *)entry->aux, max_chars + 1, L"%S", entry->desc);
	while (*wstr) {
		if (*wstr == '\\' && *(wstr + 1) == 'n') {
			*wstr = '\r';
			*(wstr + 1) = '\n';
			*wstr++;
		}
		*wstr++;
	}
}

static int g_pending_skill_id;
static unsigned char g_skill_pvp_mask;
static int g_skill_target_id;
static unsigned g_skill_stop_rel_time;
static unsigned g_rel_time;

static void
use_skill_hooked(int skill_id, unsigned char pvp_mask, int num_targets, int *target_ids)
{
	g_skill_stop_rel_time = 0;
	g_skill_pvp_mask = pvp_mask;
	g_pending_skill_id = 0;
	pw_use_skill(skill_id, pvp_mask, num_targets, target_ids);
}

static void
hooked_try_cast_skill(void)
{
	__asm__ volatile(
		"mov %0, ebx;"
		"mov %1, dword ptr [esp + 0x8];"
		: "=r"(g_skill_target_id), "=r"(g_pending_skill_id));

	if (g_skill_stop_rel_time > g_rel_time - 1500) {
		g_skill_stop_rel_time = 0;
		pw_use_skill(g_pending_skill_id, g_skill_pvp_mask, 1, &g_skill_target_id);
	}
}

static void
hooked_on_skill_end(void)
{
	int skill_id;
	struct player *player;

	__asm__(
		"mov %0, edx;"
		"mov %1, esi;"
		: "=r"(skill_id), "=r"(player));


	if (player != g_pw_data->game->player) {
		return;
	}

	g_skill_stop_rel_time = g_rel_time;

	if (g_pending_skill_id) {
		pw_use_skill(g_pending_skill_id, g_skill_pvp_mask, 1, &g_skill_target_id);
	}
}

static unsigned __thiscall
hooked_pw_game_tick(struct game_data *game, unsigned tick_time)
{
	struct thread_msg_ctx *msg;
	g_rel_time += tick_time;

	/* poll one at a time */
	msg = ring_buffer_sp_sc_pop(g_game_thr_queue);
	if (__builtin_expect(msg != NULL, 0)) {
		msg->cb(msg->arg1, msg->arg2);
		free(msg);
	}

	return pw_game_tick(game, tick_time);
}

static unsigned __stdcall
hooked_fixup_item_merging(void *frame)
{
	register int cmd_slot_amount asm("edi");
	int pack, cmd_last_slot, last_slot, slot_amount;
	int ok;

	cmd_last_slot = *(int32_t *)(frame + 0x14);
	last_slot = *(int32_t *)(frame + 0x20);
	slot_amount = *(int32_t *)(frame + 0x24);
	pack = *(int32_t *)(frame + 0x2c);

	ok = cmd_slot_amount == slot_amount && cmd_last_slot == last_slot;
	if (!ok) {
		pw_debuglog(1, "re-syncing pack %d state", pack);
		void (*refresh_inv_fn)(char inv_id) = (void *)0x5a85f0;
		refresh_inv_fn(pack);
	}
	return last_slot;
}

static void * __thiscall
hooked_get_recipe_to_display(void *this, int unk1, char unk2)
{
	static void * __thiscall
		(*real_get_recipe_to_display)(void *this, int unk1, char unk2) = (void *)0x481200;

	void *recipe = real_get_recipe_to_display(this, unk1, unk2);
	if (!recipe) {
		return NULL;
	}

	void *recipe_essence = *(void **)(recipe + 0x54);
	uint32_t tgt_id = *(uint32_t *)(recipe_essence + 0x5c);
	if (!tgt_id) {
		return NULL;
	}

	return recipe;
}

static void *__thiscall
hooked_show_world_chat_messagebox(int unk1, int unk2)
{
	static void *__thiscall (*real_fn)(int unk1, int unk2) = (void *)0x6d2df0;
	static bool __thiscall (*close_message_box_fn)(void *ui_man, int retval, void *message_box) = (void *)0x5502c0;
	void * ret;
void *dlg;

	ret = real_fn(unk1, unk2);

	dlg = pw_get_dialog(g_pw_data->game->ui->ui_manager, "Game_ChatWorld");
	close_message_box_fn(g_pw_data->game->ui->ui_manager, 6, dlg);

	return ret;
}

extern bool g_disable_all_overlay;

static bool __thiscall
hooked_pre_screenshot_render(void *this, int unk)
{
	bool __thiscall (*org_fn)(void *this, int unk) = (void *)0x431690;
	bool rc;

	g_disable_all_overlay = !g_pw_data->game->ui->ui_manager->show_ui;
	rc = org_fn(this, unk);
	g_disable_all_overlay = false;

	return rc;

}

PATCH_JMP32(0x434156, hooked_pre_screenshot_render);

static int g_detail_map_size = 800;
static int g_detail_map_org_size[] = { 800, 600 };
static int g_detail_map_tgt_size[] = { 800, 600 };

static void __thiscall
hooked_world_map_dlg_resize(struct ui_dialog *dialog, int *size)
{
	int l;
	int t;
	int r;
	int b;

	if (size) {
		l = size[0];
		t = size[1];
		r = size[2];
		b = size[3];
	} else {
		RECT rect;
		GetClientRect(g_window, &rect);
		l = rect.left;
		t = rect.top;
		r = rect.right;
		b = rect.bottom;
	}

	int w = r - l;
	int h = b - t;

	g_detail_map_org_size[0] = w;
	g_detail_map_org_size[1] = h;

	if (3 * w > h * 4) {
		int th = h;
		int tw = th * 4 / 3;
		l = 0;
		r = tw;
		g_detail_map_size = w;
		g_detail_map_tgt_size[1] = th;
		g_detail_map_tgt_size[0] = tw;
	} else {
		g_detail_map_size = h;
		g_detail_map_tgt_size[1] = w * 3 / 4;
		g_detail_map_tgt_size[0] = w;
	}

	int newsize[4] = { l, t, r, b };
	pw_world_map_dlg_resize(dialog, newsize);
}

static int __stdcall
hooked_get_detail_map_size(void *stack)
{
	int *r = stack + 0x10;
	int *l = stack + 0x8;

	/* the following code calculates (r - l) over and over, so modify those two */
	*l = 0;
	*r = g_detail_map_size;

	return g_detail_map_size * 2;
}

static int __stdcall
hooked_on_world_map_click(void *stack)
{
	float *x = stack + 0x20;
	float *y = stack + 0x10;

	pw_debuglog(1, "hooked_on_world_map_click x=%0.4f, y=%0.4f", *x, *y);
}

TRAMPOLINE(0x50c42b, 6, " \
		call org; \
		mov ecx, esp; \
		pushad; pushfd; \
		push ecx; \
		call 0x%x; \
		popfd; popad;",
		hooked_on_world_map_click);

struct pos_t {
	int x, y;
};

static void __stdcall
hooked_guild_map_pixel_to_screen(struct pos_t *in, struct pos_t *ret)
{
	ret->x = (in->x - 1024 / 2) * g_detail_map_tgt_size[1] / 768 + g_detail_map_org_size[0] / 2;
	ret->y = (in->y - 1024 / 2) * g_detail_map_tgt_size[1] / 768 + g_detail_map_org_size[1] / 2;
}

TRAMPOLINE(0x4cb585, 5, " \
		push ecx; \
		lea eax, [esp + 0x2c]; \
		push eax; \
		call 0x%x; \
		call org;",
		hooked_guild_map_pixel_to_screen);

static void __stdcall
hooked_guild_map_screen_to_pixel(struct pos_t *in, struct pos_t *ret)
{
	ret->x = (in->x - g_detail_map_org_size[0] / 2) * 768 / g_detail_map_tgt_size[1] + 1024 / 2;
	ret->y = (in->y - g_detail_map_org_size[1] / 2) * 768 / g_detail_map_tgt_size[1] + 1024 / 2;
}

TRAMPOLINE(0x4cb4c3, 6, " \
		push eax; \
		push eax; \
		lea eax, [esp + 0x30]; \
		push eax; \
		call 0x%x; \
		pop eax; \
		call org;",
		hooked_guild_map_screen_to_pixel);

static void __thiscall
hooked_bring_dialog_to_front(void *ui_manager, struct ui_dialog *dialog)
{
	if (dialog && dialog->name && strcmp(dialog->name, "Dlg_Console") == 0) {
		return;
	}

	pw_bring_dialog_to_front(ui_manager, dialog);
}

static void
parse_cmdline(void)
{
	char cmdline[256];
	char *word, *c;
	char *params[32];
	int argc = 0;
	size_t len;
	int i, rc;

	snprintf(cmdline, sizeof(cmdline), "%s", GetCommandLine());

	word = c = cmdline;
	while (*c) {
		if (*c == ' ' || *c == '=') {
			*c = 0;
			if (*word) {
				params[argc++] = word;
			}
			word = c + 1;
		}
		c++;
	}

	if (word) {
		params[argc++] = word;
	}

	char **a = params;
	while (argc > 0) {
		if (argc >= 2 && (strcmp(*a, "-p") == 0 || strcmp(*a, "--profile") == 0)) {
			snprintf(g_profile_idstr, sizeof(g_profile_idstr), "%s", *(a + 1));
			csh_cmdf("profile %s", g_profile_idstr);
			a++;
			argc--;
		}

		a++;
		argc--;
	}

	if (g_profile_id == -1) {
		g_profile_id = 1;
	}
}

static bool g_r_custom_tag_font;
CSH_REGISTER_VAR_B("r_custom_tag_font", &g_r_custom_tag_font);

bool window_hooked_init(HINSTANCE hinstance, int do_show, bool _org_is_fullscreen);
void window_reinit(void);

static int
init_hooks(void)
{
	int rc;

	setup_crash_handler(append_crash_info_cb, NULL);

	g_game_thr_queue = ring_buffer_sp_sc_new(32);
	assert(g_game_thr_queue != NULL);

	parse_cmdline();

	/* find and init some game data */
	rc = csh_init("..\\patcher\\config.txt");
	if (rc != 0) {
		MessageBox(NULL, "Can't load the config file at ../patcher/config.txt", "Error", MB_OK);
		return rc;
	}


	rc = pw_item_desc_load("..\\patcher\\item_desc.data");
	if (rc != 0) {
		MessageBox(NULL, "Failed to load item description from patcher/item_desc.data",
				"Error", MB_OK);
		return rc;
	}

	pw_avl_foreach(g_pw_item_desc_avl, item_desc_avl_wchar_fn, NULL, NULL);

	g_elements_map = pw_idmap_init("elements", "..\\patcher\\elements.imap", false);
	if (!g_elements_map) {
		/* nothing scary, just continue */
		pw_log_color(0xDD1100, "pw_idmap_init() failed");
	}

	set_pw_version();

	/* hook into window creation (before it's actually created */
	patch_jmp32(0x43aec8, (uintptr_t)window_hooked_init);

	/* don't let the game reset GWL_EXSTYLE to 0 */
	patch_mem(0x40bf43, "\x81\xc4\x0c\x00\x00\x00", 6);

	trampoline_fn((void **)&pw_load_configs, 5, hooked_pw_load_configs);

	/* hook into exit */
	patch_mem(0x43b407, "\x66\x90\xe8\x00\x00\x00\x00", 7);
	patch_jmp32(0x43b407 + 2, (uintptr_t)hooked_exit);

	/* replace the exception handler */
	patch_mem(0x417aba, "\xe9", 1);
	patch_jmp32(0x417aba, (uintptr_t)hooked_exception_handler);

	if (g_r_custom_tag_font) {
		HMODULE gdi_full_h = GetModuleHandle("gdi32full.dll");
		if (!gdi_full_h) {
			gdi_full_h = GetModuleHandle("gdi32.dll");
		}

		org_CreateFontIndirectExW = (void *)GetProcAddress(gdi_full_h, "CreateFontIndirectExW");
		trampoline_winapi_fn((void **)&org_CreateFontIndirectExW, (void *)hooked_CreateFontIndirectExW);
	}

	/* "teleport" other players only when they're moving >= 25m/s (instead of default >= 10m/s) */
	patch_mem_u32(0x442bee, (uint32_t)&g_local_max_move_speed);
	patch_mem_u32(0x442ff2, (uint32_t)&g_local_max_move_speed);
	patch_mem_u32(0x443417, (uint32_t)&g_local_max_move_speed);

	/* don't show the notice on start */
	patch_mem_u32(0x562ef8, 0x8e37bc);

	/* send movement packets more often, 500ms -> 80ms */
	patch_mem(0x44a459, "\x50\x00", 2);
	/* wait less before sending the first movement packet 200ms -> 144ms */
	patch_mem(0x44a6c9, "\x90\x00", 2);

	/* put smaller bottom limit on other player's move time (otherwise the game processes our
	 * frequent movement packets as if they were sent with bigger interval, which practically
	 * slows down the player a lot */
	patch_mem(0x442cb9, "\x50\x00", 2);
	patch_mem(0x442cc0, "\x50\x00", 2);
	/* hardcoded movement speed, originally lower than min. packet interval */
	patch_mem(0x442dec, "\x40", 1);
	patch_mem(0x443008, "\x40", 1);
	patch_mem(0x443180, "\x40", 1);
	/* increase the upper limit on other player's move time from 1s to 2s, helps on lag spikes */
	patch_mem(0x442cc8, "\xd0\x07", 2);
	patch_mem(0x442ccf, "\xd0\x07", 2);

	/* sync items with the server when de-sync is detected */
	patch_mem(0x44cd87, "\x54\x90", 2);
	patch_mem(0x44cd89, "\xe8\x00\x00\x00\x00\x89\xc5\xeb\x07", 9);
	patch_jmp32(0x44cd89, (uintptr_t)hooked_fixup_item_merging);
	/* don't show "Equipping will bind" window */
	patch_mem(0x4d8b87, "\x00", 1);
	patch_mem(0x4bc0bf, "\x00", 1);
	/* show Bound only on unable-to-trade items */
	patch_mem(0x492020, "\x10", 1);
	patch_mem(0x49205d, "\x00", 1);
	/* don't show Bound on "Doesn't drop on death" items */
	patch_mem(0x48affe, "\x10", 1);
	patch_mem(0x48b039, "\x00", 1);
	/* ^ */
	patch_mem(0x48aace, "\x10", 1);
	patch_mem(0x48ab09, "\x00", 1);
	/* ^ for equipping items from action bar */
	patch_mem(0x492de9, "\x00", 1);

	/* force screenshots via direct3d, not angellica engine */
	patch_mem(0x433e35, "\xeb", 1);

	trampoline_fn((void **)&pw_add_chat_message, 7, hooked_add_chat_message);
	//trampoline_fn((void **)&pw_console_cmd, 6, hooked_pw_console_cmd);
	trampoline_fn((void **)&pw_on_game_enter, 7, hooked_on_game_enter);
	trampoline_fn((void **)&pw_on_game_leave, 5, hooked_on_game_leave);
	trampoline_fn((void **)&pw_dialog_show, 6, hooked_on_dialog_show);
	patch_jmp32(0x6c822a, (uintptr_t)hooked_load_dialog_layout);

	/* always enable ingame console */
	patch_mem(0x927cc8, "\x01", 1);

	patch_jmp32(0x471f70, (uintptr_t)hooked_translate3dpos2screen);
	trampoline_fn((void **)&pw_item_add_ext_desc, 10, hooked_item_add_ext_desc);

	/* open fashion preview when a fashion crafting recipe is clicked */
	patch_jmp32(0x4f0238, (uintptr_t)hooked_alloc_produced_item);

	/* send next skill sending packets while the current one is still going.
	 * This reduces the lag a bit */
	//trampoline_call(0x46e7a6, 6, hooked_on_skill_end);
	//trampoline_call(0x455ce1, 6, hooked_try_cast_skill);
	//trampoline_fn((void **)&pw_use_skill, 5, use_skill_hooked);

	/* silence "Invalid skill" error message when too many packets are sent */
	//patch_mem(0x585afa, "\xeb\x12", 2);

	/* always show the number of items to be crafted (even if you cant craft atm) */
	patch_mem(0x4f0132, "\x66\x90", 2);
	/* don't show invalid recipes (tgt item id = 0) */
	patch_jmp32(0x4ef565, (uintptr_t)hooked_get_recipe_to_display);

	trampoline_fn((void **)&pw_world_map_dlg_resize, 6, hooked_world_map_dlg_resize);

	patch_mem(0x50bb00, "\x54\xe8\x00\x00\x00\x00\x90\x90\x90\x90\x90\x90\x8b\xce", 14);
	patch_jmp32(0x50bb00 + 1, (uintptr_t)hooked_get_detail_map_size);

	trampoline_fn((void **)&pw_bring_dialog_to_front, 8, hooked_bring_dialog_to_front);

	/* show bank slots >= 100 (3 digits) */
	patch_mem(0x8db72f, "3", 1);

	/* allow WC without teles */
	patch_mem(0x4b89ef, "\x90\x90", 2);

	/* auto-confirm WC message box */
	patch_jmp32(0x4b8ddd, (uintptr_t)hooked_show_world_chat_messagebox);

	patch_mem_static_init();

	return 0;
}

static void * __thiscall
hooked_open_local_cfg(void *unk, const char *path)
{
	static void * __thiscall (*org_fn)(void *unk, const char *path) = (void *)0x6fed70;
	int rc;

	/* increase libgamehook refcount so it's never unloaded
	 * (mostly for debugging / hot patching) */
	LoadLibraryA("libgamehook.dll");

	rc = init_hooks();
	if (rc != 0) {
		remove_crash_handler();
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		*(uint32_t *)0x0 = 42;
	}

	return org_fn(unk, path);
}

static unsigned __thiscall
hooked_pw_game_tick_init(struct game_data *game, unsigned tick_time)
{
	int rc;

	_patch_jmp32_unsafe(0x42bfa1, (uintptr_t)hooked_pw_game_tick);

	/* hook into PW input handling */
	window_reinit();

	return pw_game_tick(game, tick_time);
}

BOOL APIENTRY
DllMain(HMODULE mod, DWORD reason, LPVOID _reserved)
{
	unsigned i;
	int rc;

	switch (reason) {
	case DLL_PROCESS_ATTACH: {
		DisableThreadLibraryCalls(mod);
		common_static_init();

		const char dll_disable_buf[] = "\x83\xc4\x04\x83\xc8\xff";

		d3d_hook();

		if (memcmp((void *)(uintptr_t)0x43abd9, dll_disable_buf, 6) == 0) {
			rc = init_hooks();
			if (rc != 0) {
				return 0;
			}

			g_window = *(HWND *)(uintptr_t)0x927f60;
			_patch_jmp32_unsafe(0x42bfa1, (uintptr_t)hooked_pw_game_tick_init);
			return TRUE;
		}

		/* don't load gamehook.dll anymore */
		_patch_mem_unsafe(0x43abd9, "\x83\xc4\x04\x83\xc8\xff", 6);

		/* don't run pwprotector */
		_patch_mem_unsafe(0x8cfb30, "_", 1);

		/* enable fseek for > 2GB files */
		_patch_mem_u32_unsafe(0x85f454, (uintptr_t)hooked_fseek);

		/* disable default cmdline parsing */
		_patch_mem_unsafe(0x43acfb, "\xe8\xc0\x0a\x00\x00", 5);

		/* hook early into cfg file reading (before any window is even opened) */
		_patch_jmp32_unsafe(0x40b016, (uintptr_t)hooked_open_local_cfg);

		/* hook the game main loop */
		_patch_jmp32_unsafe(0x42bfa1, (uintptr_t)hooked_pw_game_tick);

		return TRUE;
	}
	case DLL_PROCESS_DETACH:
		if (g_exiting) {
			return TRUE;
		}

		pw_log_color(0xDD1100, "PW Hook unloading");

		g_unloading = true;
		d3d_unhook();

		if (!g_exiting) {
			restore_mem();
		}

		common_static_fini();
		return TRUE;
	default:
		return FALSE;
	}

	return FALSE;
}

static void __attribute__((constructor (101)))
static_preinit(void)
{
	csh_static_preinit();
}

static void __attribute__((constructor (199)))
static_postinit(void)
{
	csh_static_postinit();
}
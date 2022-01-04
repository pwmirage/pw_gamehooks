/*-
 * The MIT License
 *
 * Copyright 2019 Darek Stojaczyk
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <windows.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <wchar.h>
#include <inttypes.h>
#include <stdint.h>
#include <math.h>
#include <stdbool.h>
#include <assert.h>
#include <wingdi.h>

#include "pw_api.h"
#include "common.h"
#include "d3d.h"
#include "game_config.h"
#include "avl.h"
#include "pw_item_desc.h"
#include "extlib.h"
#include "idmap.h"

extern bool g_use_borderless;
extern unsigned g_target_dialog_pos_y;

static int g_profile_id = -1;
static char g_profile_idstr[32];
static bool g_fullscreen = false;
static bool g_sel_fullscreen = false;
bool g_replace_font = true;
static wchar_t g_version[32];
static wchar_t g_build[32];

static struct pw_idmap *g_elements_map;

struct rect {
	int x, y, w, h;
};

static struct rect g_window_size;

static void
set_borderless(bool is_borderless)
{
	int style = is_borderless ? 0x80000000 : 0x80ce0000;

	patch_mem_u32(0x40beb5, style);
	patch_mem_u32(0x40beac, style);
	SetWindowLong(g_window, GWL_STYLE, style);
}

static void
set_borderless_fullscreen(bool is_fullscreen)
{
	RECT rect;

	if (g_window_size.w == 0 || is_fullscreen) {
		/* save window position & dimensions */
		GetWindowRect(g_window, &rect);
		g_window_size.x = rect.left;
		g_window_size.y = rect.top;
		g_window_size.w = rect.right - rect.left;
		g_window_size.h = rect.bottom - rect.top;
	}

	g_fullscreen = is_fullscreen;
	g_target_dialog_pos_y = 0;

	set_borderless(is_fullscreen);
	if (is_fullscreen) {
		int fw, fh;

		fw = GetSystemMetrics(SM_CXSCREEN);
		fh = GetSystemMetrics(SM_CYSCREEN);

		/* WinAPI window styles when windowed on every win resize -> PW sets them on every resize */
		SetWindowPos(g_window, HWND_TOP, 0, 0, fw, fh, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
	} else {
		SetWindowPos(g_window, HWND_TOP,
				g_window_size.x, g_window_size.y,
				g_window_size.w, g_window_size.h,
				SWP_SHOWWINDOW | SWP_FRAMECHANGED);
	}
}

static void __stdcall
setup_fullscreen_combo(void *unk1, void *unk2, unsigned *is_fullscreen)
{
	unsigned __stdcall (*real_fn)(void *, void *, unsigned *) = (void *)0x6d5ba0;

	if (g_use_borderless) {
		*is_fullscreen = g_fullscreen;
	}

	real_fn(unk1, unk2, is_fullscreen);

	if (g_use_borderless) {
		*is_fullscreen = 0;
	}
}

static int __thiscall
hooked_read_local_cfg_opt(void *unk1, const char *section, const char *name, int def_val)
{
	int ret = pw_read_local_cfg_opt(unk1, section, name, def_val);

	if (strcmp(section, "Video") == 0) {
		if (strcmp(name, "RenderWid") == 0) {
			return game_config_get_int(g_profile_idstr, "width", 1366);
		} else if (strcmp(name, "RenderHei") == 0) {
			return game_config_get_int(g_profile_idstr, "height", 768);
		} else if (strcmp(name, "FullScreen") == 0) {
			if (g_use_borderless) {
				return 0;
			} else {
				return game_config_get_int(g_profile_idstr, "fullscreen", 0);
			}
		}
	}

	return ret;
}

static bool __thiscall
hooked_save_local_cfg_opt(void *unk1, const char *section, const char *name, int val)
{
	bool ret = pw_save_local_cfg_opt(unk1, section, name, val);

	if (strcmp(name, "RenderWid") == 0) {
		return game_config_set_int(g_profile_idstr, "width", val);
	} else if (strcmp(name, "RenderHei") == 0) {
		return game_config_set_int(g_profile_idstr, "height", val);
	} else if (strcmp(name, "FullScreen") == 0) {
		if (g_use_borderless) {
			val = g_fullscreen;
		}
		return game_config_set_int(g_profile_idstr, "fullscreen", val);
	}
	return ret;
}

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

/* button clicks / slider changes / etc */
static unsigned __stdcall
on_ui_change(const char *ctrl_name, struct ui_dialog *dialog)
{
	unsigned __stdcall (*real_fn)(const char *, void *) = (void *)0x6c9670;

	pw_log("ctrl: %s, win: %s\n", ctrl_name, dialog->name);

	if (strncmp(dialog->name, "Win_Setting", strlen("Win_Setting")) == 0 && strcmp(ctrl_name, "customsetting") == 0) {
		g_settings_show = true;
		return 1;
	}

	if (strcmp(dialog->name, "Win_Produce") == 0 && strncmp(ctrl_name, "set", 3) == 0) {
		g_ignore_next_craft_change = true;
	}

	unsigned ret = real_fn(ctrl_name, dialog);

	if (strcmp(dialog->name, "Win_SettingSystem") == 0) {
		if (strcmp(ctrl_name, "default") == 0) {
			g_sel_fullscreen = false;
		} else if (strcmp(ctrl_name, "IDCANCEL") == 0) {
			g_sel_fullscreen = g_fullscreen;
		} else if (strcmp(ctrl_name, "apply") == 0 || strcmp(ctrl_name, "confirm") == 0) {
			if (g_use_borderless) {
				pw_log("sel: %d, real: %d\n", g_sel_fullscreen, g_fullscreen);
				if (g_sel_fullscreen != g_fullscreen) {
					set_borderless_fullscreen(g_sel_fullscreen);
				}
			}
		}
	}

	return ret;
}

static bool g_in_dialog_layout_load = false;

static void __thiscall
hooked_on_dialog_show(struct ui_dialog *dialog, bool do_show, bool is_modal, bool is_active)
{
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

static unsigned __fastcall
on_combo_change(void *ctrl)
{
	unsigned __fastcall (*real_fn)(void *) = (void *)0x6e1c90;
	const char *ctrl_name = *(const char **)(ctrl + 0x14);
	int selection = *(int *)(ctrl + 0xa0);
	void *parent_win = *(void **)(ctrl + 0xc);
	const char *parent_name = *(const char **)(parent_win + 0x28);

	pw_log("combo: %s, selection: %u, win: %s\n", ctrl_name, selection, parent_name);

	if (strcmp(parent_name, "Win_SettingSystem") == 0 && strcmp(ctrl_name, "Combo_Full") == 0) {
		g_sel_fullscreen = !!selection;
	}

	return real_fn(ctrl);
}

static float
dist_obj(struct object *obj1, struct object *obj2)
{
	float dist_tmp, dist;

	dist_tmp = obj1->pos_x - obj2->pos_x;
	dist_tmp *= dist_tmp;
	dist = dist_tmp;

	dist_tmp = obj1->pos_z - obj2->pos_z;
	dist_tmp *= dist_tmp;
	dist += dist_tmp;

	dist_tmp = obj1->pos_y - obj2->pos_y;
	dist_tmp *= dist_tmp;
	dist += dist_tmp;

	return sqrt(dist);
}

static void
select_closest_mob(void)
{
	struct game_data *game = g_pw_data->game;
	struct player *player = game->player;
	uint32_t mobcount = game->wobj->moblist->count;
	float min_dist = 999;
	uint32_t new_target_id = player->target_id;
	struct mob *mob;

	for (int i = 0; i < mobcount; i++) {
		float dist;

		mob = game->wobj->moblist->mobs->mob[i];
		if (mob->obj.type != 6) {
			/* not a monster */
			continue;
		}

		if (mob->disappear_count > 0) {
			/* mob is dead and already disappearing */
			continue;
		}

		dist = dist_obj(&mob->obj, &player->obj);
		if (dist < min_dist) {
			min_dist = dist;
			new_target_id = mob->id;
		}
	}

	pw_select_target(new_target_id);
}

static WNDPROC g_orig_event_handler;

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

#define MG_CB_MSG (WM_USER + 165)

typedef void (*mg_callback)(void *arg1, void *arg2);

struct ui_thread_ctx {
	mg_callback cb;
	void *arg1;
	void *arg2;
};

void
pw_ui_thread_sendmsg(mg_callback cb, void *arg1, void *arg2)
{
	struct ui_thread_ctx *ctx = malloc(sizeof(*ctx));
	assert(ctx != NULL);
	ctx->cb = cb;
	ctx->arg1 = arg1;
	ctx->arg2 = arg2;
	SendMessage(g_window, MG_CB_MSG, 0, (LPARAM)ctx);
}

void
pw_ui_thread_postmsg(mg_callback cb, void *arg1, void *arg2)
{
	struct ui_thread_ctx *ctx = malloc(sizeof(*ctx));
	assert(ctx != NULL);
	ctx->cb = cb;
	ctx->arg1 = arg1;
	ctx->arg2 = arg2;
	PostMessage(g_window, MG_CB_MSG, 0, (LPARAM)ctx);
}

static bool
is_mouse_over_window(int safe_margin)
{
	POINT mouse_pos;
	RECT win_pos;

	GetWindowRect(g_window, &win_pos);
	GetCursorPos(&mouse_pos);

	if (mouse_pos.x > win_pos.left + safe_margin &&
			mouse_pos.x < win_pos.right - safe_margin &&
			mouse_pos.y > win_pos.top + safe_margin &&
			mouse_pos.y < win_pos.bottom - safe_margin) {
		return true;
	}

	return false;
}

static LRESULT CALLBACK
event_handler(HWND window, UINT event, WPARAM data, LPARAM lparam)
{
	switch(event) {
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
	case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	case WM_XBUTTONUP:
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	case WM_MOUSEACTIVATE:
		if (d3d_handle_mouse(event, data, lparam)) {
			return TRUE;
		}
		break;
	case WM_SYSKEYDOWN:
	case WM_KEYDOWN:
		switch (data) {
			case VK_LWIN:
			case VK_RWIN:
				if (g_use_borderless && g_fullscreen &&
						GetActiveWindow() == g_window &&
						is_mouse_over_window(5)) {
					ShowWindow(g_window, SW_MINIMIZE);
					g_pw_data->is_render_active = false;
				}
				break;
			default:
				break;
		}
		if (d3d_handle_keyboard(event, data, lparam)) {
			return TRUE;
		}
		break;
	case WM_SYSKEYUP:
		if (data == VK_RETURN) {
			if (g_use_borderless) {
				set_borderless_fullscreen(!g_fullscreen);
			}
		}
		if (d3d_handle_keyboard(event, data, lparam)) {
			return TRUE;
		}
		break;
	case WM_KEYUP:
	case WM_CHAR:
	case WM_DEVICECHANGE:
		if (d3d_handle_keyboard(event, data, lparam)) {
			return TRUE;
		}
		break;
	case WM_KILLFOCUS:
		if ((GetAsyncKeyState(VK_MENU) & 0x8000) && g_use_borderless && g_fullscreen &&
				is_mouse_over_window(5)) {
			ShowWindow(g_window, SW_MINIMIZE);
			g_pw_data->is_render_active = false;
		}
		break;
	case WM_SYSCOMMAND:
		switch (data) {
		case SC_MINIMIZE:
			/* PW doesnt react to this message and keeps using CPU, so make it stop */
			g_pw_data->is_render_active = false;
			break;
		case SC_RESTORE:
			g_pw_data->is_render_active = true;
			break;
		default:
			break;
		}
		break;
	case WM_MENUCHAR:
		CallWindowProc(g_orig_event_handler, window, event, data, lparam);
		/* do not beep! */
		return MNC_CLOSE << 16;
	case MG_CB_MSG: {
		struct ui_thread_ctx ctx, *org_ctx = (void *)lparam;

		ctx = *org_ctx;
		free(org_ctx);
		ctx.cb(ctx.arg1, ctx.arg2);
		break;
	}
	default:
		break;
	}

	/* let the game handle this key */
	return CallWindowProc(g_orig_event_handler, window, event, data, lparam);
}

static void
check_and_minize_win_cb(void *arg1, void *arg2)
{
	if (g_use_borderless && g_fullscreen && GetActiveWindow() != g_window &&
			is_mouse_over_window(5)) {
		ShowWindow(g_window, SW_MINIMIZE);
		g_pw_data->is_render_active = false;
	}
}

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

	snwprintf(g_version, sizeof(g_version) / sizeof(wchar_t), L"	   PW Mirage");
	snwprintf(g_build, sizeof(g_build) / sizeof(wchar_t), L"	  v%d", version);

	patch_mem_u32(0x563c6c, (uintptr_t)g_version);
	patch_mem_u32(0x563cb6, (uintptr_t)g_build);
}

static void __thiscall
hooked_add_chat_message(void *cecgamerun, const wchar_t *str, char channel, int player_id, const wchar_t *name, char unk, char emote)
{
	if (channel == 12) {
		pw_log("received (%d): %S", channel, str);
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

	d3d_init_settings(D3D_INIT_SETTINGS_PLAYER_LOAD);

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
void
parse_console_cmd(const char *in, char *out, size_t outlen)
{

	if (_strnicmp(in, "d ", 2) == 0) {
		_snprintf(out, outlen, "d_c2scmd %s", in + 2);
	} else if (_strnicmp(in, "di ", 3) == 0) {
		struct pw_idmap_el *node;
		unsigned pid = 0, id = 0;
		uint64_t lid;
		int rc;

		const char *hash_str = strstr(in, "#");
		if (hash_str) {
			in = hash_str + 1;
		} else {
			in = in + 3;
		}

		rc = sscanf(in, "%d : %d", &pid, &id);
		if (rc == 2 && g_elements_map) {
			lid = (pid > 0 ? 0x80000000 : 0) + 0x100000 * pid + id;
			_snprintf(out, outlen, "d_c2scmd 10800 %d", pw_idmap_get_mapping(g_elements_map, lid, 0));
		} else {
			_snprintf(out, outlen, "d_c2scmd 10800 %s", in);
		}
	} else {
		_snprintf(out, outlen, "%s", in);
	}
}

static void
hooked_pw_get_info_on_acquire(unsigned char inv_id, unsigned char slot_id)
{
    unsigned *expire_time;
    unsigned need_info;

    __asm__(
            "mov %0, eax;"
            : "=r"(need_info));

    if (need_info) {
        pw_get_item_info(inv_id, slot_id);
        return;
    }

    /* read the upper frame's stack */
    expire_time = (unsigned *)(&expire_time + 11);
    if (*expire_time == 0x631b) {
        /* hooked magic number, this item came from a task and the above time is not valid yet */
        *expire_time = 0;
        pw_get_item_info(inv_id, slot_id);
    }
}

bool g_exiting = false;
bool g_unloading = false;
static float g_local_max_move_speed = 25.0f;

static void uninit_cb(void *arg1, void *arg2);

static void
hooked_exit(void)
{
	g_unloading = true;
	g_exiting = true;
	g_replace_font = false;

	/* our hacks sometimes crash on exit, not sure why. they're hacks, so just ignore the errors */
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	uninit_cb(NULL, NULL);
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
	g_rel_time += tick_time;
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
		pw_log("re-syncing pack %d state", pack);
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

static bool __thiscall
hooked_pw_on_keydown(void *ui_manager, int event, int keycode, unsigned mods)
{
	bool is_repeat = mods & 0x40000000;

	switch(event) {
	case WM_CHAR:
		switch (keycode) {
			case '~': {
				if (!is_repeat) {
					d3d_console_toggle();
				}
				return true;
			}
			default:
				break;
		}
		break;
	case WM_KEYDOWN:
		switch (keycode) {
			case VK_TAB:
				if (!is_repeat) {
					select_closest_mob();
				}
				return true;
			default:
				break;
		}
		break;
	default:
		break;
	}

	bool rc = pw_on_keydown(ui_manager, event, keycode, mods);
	return rc;
}

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

	pw_log("hooked_on_world_map_click x=%0.4f, y=%0.4f", *x, *y);
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

static bool
hooked_init_window(HINSTANCE hinstance, int do_show, bool _org_is_fullscreen)
{
	int rc;
	int styles;

	int x = game_config_get_int(g_profile_idstr, "x", -1);
	int y = game_config_get_int(g_profile_idstr, "y", -1);
	int w = game_config_get_int(g_profile_idstr, "width", -1);
	int h = game_config_get_int(g_profile_idstr, "height", -1);
	g_fullscreen = game_config_get_int(g_profile_idstr, "fullscreen", 0);
	if (w == -1 && h == -1) {
		w = *(int *)0x927d82;
		h = *(int *)0x927d86;
	} else {
		*(int *)0x927d82 = w;
		*(int *)0x927d86 = h;
	}

	if (g_fullscreen && g_use_borderless) {
		styles = 0x80000000;
	} else {
		styles = 0x80ce0000;
	}


	if (!g_fullscreen) {
		RECT rect = { 0, 0, w, h };
		AdjustWindowRect(&rect, styles, false);

		w = rect.right - rect.left;
		h = rect.bottom - rect.top;
		if (x == -1 && y == -1) {
			x = (GetSystemMetrics(SM_CXFULLSCREEN) - w) / 2;
			y = (GetSystemMetrics(SM_CYFULLSCREEN) - h) / 2;
		}
	} else if (x == -1 && y == -1) {
		x = 0;
		y = 0;
	}

	g_window_size.x = x;
	g_window_size.y = y;
	g_window_size.w = w;
	g_window_size.h = h;

	g_window = CreateWindowEx(0, "ElementClient Window", "PW Mirage", styles,
			x, y, w, h, NULL, NULL, hinstance, NULL);
	if (!g_window) {
		return false;
	}

	if (g_use_borderless && g_fullscreen) {
		patch_mem_u32(0x40beb5, 0x80000000);
		patch_mem_u32(0x40beac, 0x80000000);
	}

	/* hook into PW input handling */
	g_orig_event_handler = (WNDPROC)SetWindowLong(g_window, GWL_WNDPROC, (LONG)event_handler);

	/* used by PW */
	*(HINSTANCE *)0x927f5c = hinstance;
	*(HWND *)(uintptr_t)0x927f60 = g_window;

	ShowWindow(g_window, SW_SHOW);
	UpdateWindow(g_window);
	SetForegroundWindow(g_window);

	/* force the window into foreground */
	DWORD d = 0;
	DWORD windowThreadProcessId = GetWindowThreadProcessId(GetForegroundWindow(), &d);
	DWORD currentThreadId = GetCurrentThreadId();
	AttachThreadInput(windowThreadProcessId, currentThreadId, true);
	BringWindowToTop(g_window);
	ShowWindow(g_window, SW_SHOW);
	AttachThreadInput(windowThreadProcessId,currentThreadId, false);

	return true;
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
			g_profile_id = atoi(*(a + 1));
			a++;
			argc--;
		}

		a++;
		argc--;
	}

	if (g_profile_id == -1) {
		g_profile_id = 1;
	}
	snprintf(g_profile_idstr, sizeof(g_profile_idstr), "Profile %d", g_profile_id);

}

static int
init_hooks(void)
{
	int rc;

	setup_crash_handler(append_crash_info_cb, NULL);

	parse_cmdline();

	/* find and init some game data */
	rc = game_config_parse("..\\patcher\\game.cfg");
	if (rc != 0) {
		MessageBox(NULL, "Can't load the config file at ../patcher/game.cfg", "Error", MB_OK);
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

	d3d_init_settings(D3D_INIT_SETTINGS_INITIAL);
	g_fullscreen = game_config_get_int(g_profile_idstr, "fullscreen", 0);

	/* hook into window creation (before it's actually created */
	patch_jmp32(0x43aec8, (uintptr_t)hooked_init_window);

	/* don't let the game reset GWL_EXSTYLE to 0 */
	patch_mem(0x40bf43, "\x81\xc4\x0c\x00\x00\x00", 6);

	trampoline_fn((void **)&pw_read_local_cfg_opt, 5, hooked_read_local_cfg_opt);
	trampoline_fn((void **)&pw_save_local_cfg_opt, 8, hooked_save_local_cfg_opt);
	trampoline_fn((void **)&pw_load_configs, 5, hooked_pw_load_configs);

	/* hook into exit */
	patch_mem(0x43b407, "\x66\x90\xe8\x00\x00\x00\x00", 7);
	patch_jmp32(0x43b407 + 2, (uintptr_t)hooked_exit);

	/* replace the exception handler */
	patch_mem(0x417aba, "\xe9", 1);
	patch_jmp32(0x417aba, (uintptr_t)hooked_exception_handler);

	if (game_config_get_int("Global", "custom_tag_font", 0)) {
		HMODULE gdi_full_h = GetModuleHandle("gdi32full.dll");
		if (!gdi_full_h) {
			gdi_full_h = GetModuleHandle("gdi32.dll");
		}

		org_CreateFontIndirectExW = (void *)GetProcAddress(gdi_full_h, "CreateFontIndirectExW");
		trampoline_winapi_fn((void **)&org_CreateFontIndirectExW, (void *)hooked_CreateFontIndirectExW);
	}

	patch_jmp32(0x55006d, (uintptr_t)on_ui_change);
	patch_jmp32(0x6e099b, (uintptr_t)on_combo_change);
	patch_jmp32(0x4faea2, (uintptr_t)setup_fullscreen_combo);
	patch_jmp32(0x4faec1, (uintptr_t)setup_fullscreen_combo);


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

	/* always try to get detailed item info on accquire */
	patch_mem(0x44cdae, "\x66\x90", 2);
	patch_mem(0x44cd41, "\x1b\x63", 2);
	patch_jmp32(0x44cdb6, (uintptr_t)hooked_pw_get_info_on_acquire);

	patch_jmp32(0x471f70, (uintptr_t)hooked_translate3dpos2screen);
	trampoline_fn((void **)&pw_item_add_ext_desc, 10, hooked_item_add_ext_desc);

	/* open fashion preview when a fashion crafting recipe is clicked */
	patch_jmp32(0x4f0238, (uintptr_t)hooked_alloc_produced_item);

	/* send next skill sending packets while the current one is still going.
	 * This reduces the lag a bit */
	//trampoline_call(0x46e7a6, 6, hooked_on_skill_end);
	//trampoline_call(0x455ce1, 6, hooked_try_cast_skill);
	//trampoline_fn((void **)&pw_use_skill, 5, use_skill_hooked);
	//trampoline_fn((void **)&pw_game_tick, 5, hooked_pw_game_tick);

	/* silence "Invalid skill" error message when too many packets are sent */
	//patch_mem(0x585afa, "\xeb\x12", 2);

	/* always show the number of items to be crafted (even if you cant craft atm) */
	patch_mem(0x4f0132, "\x66\x90", 2);
	/* don't show invalid recipes (tgt item id = 0) */
	patch_jmp32(0x4ef565, (uintptr_t)hooked_get_recipe_to_display);

	trampoline_fn((void **)&pw_on_keydown, 7, hooked_pw_on_keydown);
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

	trampoline_static_init();

	d3d_hook();

	return 0;
}

static void
_patch_mem_unsafe(uintptr_t addr, const char *buf, unsigned num_bytes)
{
	DWORD prevProt, prevProt2;

	VirtualProtect((void *)addr, num_bytes, PAGE_EXECUTE_READWRITE, &prevProt);
	memcpy((void *)addr, buf, num_bytes);
	VirtualProtect((void *)addr, num_bytes, prevProt, &prevProt2);
}

static void
_patch_mem_u32_unsafe(uintptr_t addr, uint32_t u32)
{
	union {
		char c[4];
		uint32_t u;
	} u;

	u.u = u32;
	patch_mem(addr, u.c, 4);
}

static void
_patch_jmp32_unsafe(uintptr_t addr, uintptr_t fn)
{
	patch_mem_u32(addr + 1, fn - addr - 5);
}

static void * __thiscall
hooked_open_local_cfg(const char *path)
{
	static void * __thiscall (*org_fn)(const char *path) = (void *)0x6fed70;
	int rc;

	rc = init_hooks();
	if (rc != 0) {
		remove_crash_handler();
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		*(uint32_t *)0x0 = 42;
	}

	return org_fn(path);
}

static unsigned __thiscall
hooked_pw_game_tick_init(struct game_data *game, unsigned tick_time)
{
	int rc;

	_patch_jmp32_unsafe(0x42bfa1, (uintptr_t)pw_game_tick);

	g_window = *(HWND *)(uintptr_t)0x927f60;

	rc = init_hooks();
	if (rc != 0) {
		return 0;
	}

	/* hook into PW input handling */
	g_orig_event_handler = (WNDPROC)SetWindowLong(g_window, GWL_WNDPROC, (LONG)event_handler);

	return pw_game_tick(game, tick_time);
}

static void
uninit_cb(void *arg1, void *arg2)
{
	game_config_save(true);

	pw_log_color(0xDD1100, "PW Hook unloading");

	SetWindowLong(g_window, GWL_WNDPROC, (LONG)g_orig_event_handler);

	g_unloading = true;
	d3d_unhook();
}

BOOL APIENTRY
DllMain(HMODULE mod, DWORD reason, LPVOID _reserved)
{
	unsigned i;

	switch (reason) {
	case DLL_PROCESS_ATTACH: {
		DisableThreadLibraryCalls(mod);
		common_static_init();

		const char dll_disable_buf[] = "\x83\xc4\x04\x83\xc8\xff";

		if (memcmp((void *)(uintptr_t)0x43abd9, dll_disable_buf, 6) == 0) {
			_patch_jmp32_unsafe(0x42bfa1, (uintptr_t)hooked_pw_game_tick_init);
			return TRUE;
		}

		/* don't load gamehook.dll anymore */
		_patch_mem_unsafe(0x43abd9, "\x83\xc4\x04\x83\xc8\xff", 6);

		/* don't run pwprotector */
		patch_mem(0x8cfb30, "_", 1);

		/* enable fseek for > 2GB files */
		_patch_mem_u32_unsafe(0x85f454, (uintptr_t)hooked_fseek);

		/* disable default cmdline parsing */
		_patch_mem_unsafe(0x43acfb, "\xe8\xc0\x0a\x00\x00", 5);

		/* hook early into cfg file reading (before any window is even opened) */
		_patch_jmp32_unsafe(0x40b016, (uintptr_t)hooked_open_local_cfg);

		return TRUE;
	}
	case DLL_PROCESS_DETACH:
		pw_ui_thread_sendmsg(uninit_cb, NULL, NULL);

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

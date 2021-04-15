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

#include "pw_api.h"
#include "common.h"
#include "d3d.h"

extern bool g_use_borderless;

static bool g_fullscreen = false;
static bool g_sel_fullscreen = false;
static wchar_t g_version[32];
static wchar_t g_build[32];
static HMODULE g_unload_event;

static void
set_borderless_fullscreen(bool is_fullscreen)
{
	static int x, y, w, h;
	RECT rect;

	if (w == 0 || is_fullscreen) {
		/* save window position & dimensions */
		GetWindowRect(g_window, &rect);
		x = rect.left;
		y = rect.top;
		w = rect.right - rect.left;
		h = rect.bottom - rect.top;
	}

	g_fullscreen = is_fullscreen;
	if (is_fullscreen) {
		int fw, fh;

		fw = GetSystemMetrics(SM_CXSCREEN);
		fh = GetSystemMetrics(SM_CYSCREEN);

		/* WinAPI window styles when windowed on every win resize -> PW sets them on every resize */
		patch_mem_u32(0x40beb5, 0x80000000);
		patch_mem_u32(0x40beac, 0x80000000);
		SetWindowLong(g_window, GWL_STYLE, 0x80000000);
		SetWindowPos(g_window, HWND_TOP, 0, 0, fw, fh, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
	} else {
		patch_mem_u32(0x40beb5, 0x80ce0000);
		patch_mem_u32(0x40beac, 0x80ce0000);
		SetWindowLong(g_window, GWL_STYLE, 0x80ce0000);
		SetWindowPos(g_window, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_NOSIZE);
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

static unsigned __thiscall
read_fullscreen_opt(void *unk1, void *unk2, void *unk3, void *unk4)
{
	unsigned __thiscall (*real_fn)(void *, void *, void *, void *) = (void *)0x6ff590;
	unsigned is_fullscreen = real_fn(unk1, unk2, unk3, unk4);

	if (g_use_borderless) {
		g_sel_fullscreen = is_fullscreen;
		/* always return false */
		return 0;
	}

	return is_fullscreen;
}

static unsigned __stdcall
save_fullscreen_opt(void *unk1, void *unk2, unsigned is_fullscreen)
{
	unsigned __stdcall (*real_fn)(void *, void *, unsigned) = (void *)0x6ff810;
	return real_fn(unk1, unk2, g_fullscreen);
}

/* button clicks / slider changes / etc */
static unsigned __stdcall
on_ui_change(const char *ctrl_name, void *parent_win)
{
	unsigned __stdcall (*real_fn)(const char *, void *) = (void *)0x6c9670;
	const char *parent_name = *(const char **)(parent_win + 0x28);

	pw_log("ctrl: %s, win: %s\n", ctrl_name, parent_name);

	if (strcmp(parent_name, "Win_Main3") == 0 && strcmp(ctrl_name, "wquickkey") == 0) {
		g_settings_show = !g_settings_show;
		return 1;
	}

	unsigned ret = real_fn(ctrl_name, parent_win);

	if (strcmp(parent_name, "Win_SettingSystem") == 0) {
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

static LRESULT CALLBACK
event_handler(HWND window, UINT event, WPARAM data, LPARAM lparam)
{
	if (d3d_handle_input(event, data, lparam)) {
		return TRUE;
	}

	if (!g_pw_data || !g_pw_data->game || g_pw_data->game->logged_in != 2) {
		/* let the game handle this key */
		return CallWindowProc(g_orig_event_handler, window, event, data, lparam);
	}

	switch(event) {
	case WM_KEYDOWN:
		if (data == VK_TAB) {
			select_closest_mob();
			return TRUE;
		}
		break;
	default:
		break;
	}

	/* let the game handle this key */
	return CallWindowProc(g_orig_event_handler, window, event, data, lparam);
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
hooked_add_chat_message(void *cecgamerun, const wchar_t *str, char channel, int idPlayer, int szName, char byFlag, char emotion)
{
	if (channel == 12) {
		pw_log("received (%d): %S", channel, str);
		if (wcscmp(str, L"update") == 0) {
			g_update_show = true;
		}
		return;
	}

	pw_add_chat_message(cecgamerun, str, channel, idPlayer, szName, byFlag, emotion);
}

static wchar_t g_win_title[128];

DWORD __stdcall
hooked_pw_load_configs_cb(void *arg)
{
	SetWindowTextW(g_window, g_win_title);
}

static unsigned __thiscall
hooked_pw_load_configs(struct game_data *game, void *unk1, int unk2)
{
	DWORD tid;

	unsigned ret = pw_load_configs(game, unk1, unk2);

	pw_populate_console_log();

	/* always enable ingame console (could have been disabled by the game at its init) */
	patch_mem(0x927cc8, "\x01", 1);

	wchar_t *player_name = *(wchar_t **)(((char *)game->player) + 0x5cc);
	snwprintf(g_win_title, sizeof(g_win_title) / sizeof(g_win_title[0]), L"PW Mirage: %s", player_name);
	/* the process hangs if we update the title from this thread... */
	CreateThread(NULL, 0, hooked_pw_load_configs_cb, NULL, 0, &tid);
	return ret;
}

static DWORD g_tid;
static bool g_tid_finished = false;
bool g_exiting = false;
bool g_unloading = false;
static float g_local_max_move_speed = 25.0f;

static void
hooked_exit(void)
{
	g_unloading = true;
	g_exiting = true;

	/* our hacks sometimes crash on exit, not sure why. they're hacks, so just ignore the errors */
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

	SetEvent(g_unload_event);
}

static DWORD WINAPI
ThreadMain(LPVOID _unused)
{
	MSG msg;

	/* find and init some game data */

	if (pw_find_pwi_game_data() == 0) {
		MessageBox(NULL, "Failed to find PW process. Is the game running?", "Status", MB_OK);
		return 1;
	}

	set_pw_version();

	patch_mem(0x40bf43, "\x81\xc4\x0c\x00\x00\x00", 6);

	patch_jmp32(0x40b257, (uintptr_t)read_fullscreen_opt);
	patch_jmp32(0x40b842, (uintptr_t)save_fullscreen_opt);
	patch_jmp32(0x55006d, (uintptr_t)on_ui_change);
	patch_jmp32(0x6e099b, (uintptr_t)on_combo_change);
	patch_jmp32(0x4faea2, (uintptr_t)setup_fullscreen_combo);
	patch_jmp32(0x4faec1, (uintptr_t)setup_fullscreen_combo);

	/* don't run creportbugs on crash */
	patch_mem(0x8cfb40, "_", 1);
	/* don't run pwprotector */
	patch_mem(0x8cfb30, "_", 1);

	trampoline_fn((void **)&pw_load_configs, 5, hooked_pw_load_configs);

	patch_mem(0x43b407, "\x66\x90\xe8\x00\x00\x00\x00", 7);
	patch_jmp32(0x43b407 + 2, (uintptr_t)hooked_exit);

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

	/* force screenshots via direct3d, not angellica engine */
	patch_mem(0x433e35, "\xeb", 1);

	trampoline_fn((void **)&pw_add_chat_message, 7, hooked_add_chat_message);

	if (pw_wait_for_win() == 0) {
		MessageBox(NULL, "Failed to find the PW game window", "Status", MB_OK);
		return 1;
	}

	if (g_sel_fullscreen) {
		set_borderless_fullscreen(g_sel_fullscreen);
	}

	/* always enable ingame console */
	patch_mem(0x927cc8, "\x01", 1);

	d3d_hook();

	/* hook into PW input handling */
	g_orig_event_handler = (WNDPROC)SetWindowLong(g_window, GWL_WNDPROC, (LONG)event_handler);

	g_unload_event = CreateEvent(NULL, TRUE, FALSE, NULL);

	/* process our custom windows input */
	WaitForSingleObject(g_unload_event, (DWORD)INFINITY);

	SetWindowLong(g_window, GWL_WNDPROC, (LONG)g_orig_event_handler);

	if (g_exiting) {
		/* no need to cleanup anything */
		g_tid_finished = true;
		return 0;
	}

	pw_log_color(0xDD1100, "PW Hook unloading");

	g_unloading = true;

	restore_mem();
	g_tid_finished = true;
	return 0;
}

BOOL APIENTRY
DllMain(HMODULE mod, DWORD reason, LPVOID _reserved)
{
	unsigned i;

	switch (reason) {
	case DLL_PROCESS_ATTACH: {
		DisableThreadLibraryCalls(mod);
		CreateThread(NULL, 0, ThreadMain, NULL, 0, &g_tid);
		return TRUE;
	}
	case DLL_PROCESS_DETACH:
		PostThreadMessageA(g_tid, WM_QUIT, 0, 0);
		SetEvent(g_unload_event);

		/* wait for cleanup (not necessarily thread termination) */
		while (!g_unloading && !g_tid_finished) {
			Sleep(50);
		}

		return TRUE;
	default:
		return FALSE;
	}

	return FALSE;
}

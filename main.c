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
#include <inttypes.h>
#include <stdint.h>
#include <math.h>
#include <stdbool.h>
#include <assert.h>

#include "pw_api.h"

static bool g_fullscreen = false;
static bool g_sel_fullscreen = false;

static void
toggle_borderless_fullscreen(void)
{
	static int x, y, w, h;

	g_fullscreen = !g_fullscreen;
	if (g_fullscreen) {
		int fw, fh;
		RECT rect;

		fw = GetSystemMetrics(SM_CXSCREEN);
		fh = GetSystemMetrics(SM_CYSCREEN);

		GetWindowRect(g_window, &rect);
		x = rect.left;
		y = rect.top;
		w = rect.right - rect.left;
		h = rect.bottom - rect.top;

		/* WinAPI window styles when windowed on every win resize -> PW sets them on every resize */
		patch_mem_u32(0x40beb5, 0x80000000);
		patch_mem_u32(0x40beac, 0x80000000);
		SetWindowLong(g_window, GWL_STYLE, 0x80000000);
		SetWindowPos(g_window, HWND_TOP, 0, 0, fw, fh, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
	} else {
		patch_mem_u32(0x40beb5, 0x80ce0000);
		patch_mem_u32(0x40beac, 0x80ce0000);
		SetWindowLong(g_window, GWL_STYLE, 0x80ce0000);
		SetWindowPos(g_window, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
	}
}

static void __stdcall
setup_fullscreen_combo(void *unk1, void *unk2, unsigned *is_fullscreen)
{
	unsigned __stdcall (*real_fn)(void *, void *, unsigned *) = (void *)0x6d5ba0;

	*is_fullscreen = g_fullscreen;
	real_fn(unk1, unk2, is_fullscreen);
	*is_fullscreen = 0;
}

static unsigned __thiscall
read_fullscreen_opt(void *unk1, void *unk2, void *unk3, void *unk4)
{
	unsigned __thiscall (*real_fn)(void *, void *, void *, void *) = (void *)0x6ff590;
	unsigned is_fullscreen = real_fn(unk1, unk2, unk3, unk4);

	g_sel_fullscreen = is_fullscreen;
	fprintf(stderr, "fullscreen: %d\n", g_sel_fullscreen);
	/* always return false */
	return 0;
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

	fprintf(stderr, "ctrl: %s, win: %s\n", ctrl_name, parent_name);

	if (strcmp(parent_name, "Win_Main3") == 0 && strcmp(ctrl_name, "wquickkey") == 0) {
		toggle_borderless_fullscreen();
		return 1;
	}

	unsigned ret = real_fn(ctrl_name, parent_win);

	if (strcmp(parent_name, "Win_SettingSystem") == 0) {
		if (strcmp(ctrl_name, "default") == 0) {
			g_sel_fullscreen = false;
		} else if (strcmp(ctrl_name, "IDCANCEL") == 0) {
			g_sel_fullscreen = g_fullscreen;
		} else if (strcmp(ctrl_name, "apply") == 0 || strcmp(ctrl_name, "confirm") == 0) {
			fprintf(stderr, "sel: %d, real: %d\n", g_sel_fullscreen, g_fullscreen);
			if (g_sel_fullscreen != g_fullscreen) {
				toggle_borderless_fullscreen();
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

	fprintf(stderr, "combo: %s, selection: %u, win: %s\n", ctrl_name, selection, parent_name);

	if (strcmp(parent_name, "Win_SettingSystem") == 0 && strcmp(ctrl_name, "Combo_Full") == 0) {
		g_sel_fullscreen = !!selection;
	}

	return real_fn(ctrl);
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
		float dist, dist_tmp;

		mob = game->wobj->moblist->mobs->mob[i];
		if (mob->type != 6) {
			/* not a monster */
			continue;
		}

		if (mob->disappear_count > 0) {
			/* mob is dead and already disappearing */
			continue;
		}

		dist_tmp = mob->pos_x - player->pos_x;
		dist_tmp *= dist_tmp;
		dist = dist_tmp;

		dist_tmp = mob->pos_z - player->pos_z;
		dist_tmp *= dist_tmp;
		dist += dist_tmp;

		dist_tmp = mob->pos_y - player->pos_y;
		dist_tmp *= dist_tmp;
		dist += dist_tmp;

		dist = sqrt(dist);
		if (dist < min_dist) {
			min_dist = dist;
			new_target_id = mob->id;
		}
	}

	pw_select_target(new_target_id);
}

static WNDPROC g_orig_event_handler;

static LRESULT CALLBACK
event_handler(HWND window, UINT event, WPARAM data, LPARAM _unused)
{
	if (!g_pw_data || !g_pw_data->game || g_pw_data->game->logged_in != 2) {
		/* let the game handle this key */
		return CallWindowProc(g_orig_event_handler, window, event, data, _unused);
	}

	switch(event) {
	case WM_KEYDOWN:
		if (data == VK_TAB) {
			select_closest_mob();
			return TRUE;
		} else if (data == VK_PAUSE) {
			int fw, fh;

			fw = GetSystemMetrics(SM_CXSCREEN);
			fh = GetSystemMetrics(SM_CYSCREEN);

			g_fullscreen = !g_fullscreen;
			if (!g_fullscreen) {
				/* WinAPI window styles when windowed on every win resize -> PW sets them on every resize */
				patch_mem_u32(0x40beb5, 0x80000000);
				patch_mem_u32(0x40beac, 0x80000000);
				SetWindowLong(g_window, GWL_STYLE, 0x80000000);
				SetWindowPos(g_window, HWND_TOP, 0, 0, fw, fh, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
			} else if (g_fullscreen) {
				patch_mem_u32(0x40beb5, 0x80ce0000);
				patch_mem_u32(0x40beac, 0x80ce0000);
				SetWindowLong(g_window, GWL_STYLE, 0x80ce0000);
				SetWindowPos(g_window, HWND_TOP, 0, 0, fw - 100, fh - 100, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
			}
		}
		break;
	default:
		break;
	}

	/* let the game handle this key */
	return CallWindowProc(g_orig_event_handler, window, event, data, _unused);
}

static DWORD WINAPI
ThreadMain(LPVOID _unused)
{
	pw_static_init();
	/* find and init some game data */

	if (pw_find_pwi_game_data() == 0) {
		MessageBox(NULL, "Failed to find PW process. Is the game running?", "Status", MB_OK);
		return 1;
	}

	pw_spawn_debug_window();

	patch_mem_u32(0x40b258, (uintptr_t)read_fullscreen_opt - 0x40b257 - 5);
	patch_mem_u32(0x40b843, (uintptr_t)save_fullscreen_opt - 0x40b842 - 5);
	patch_mem_u32(0x55006e, (uintptr_t)on_ui_change - 0x55006d - 5);
	patch_mem_u32(0x6e099c, (uintptr_t)on_combo_change - 0x6e099b - 5);
	patch_mem_u32(0x4faea3, (uintptr_t)setup_fullscreen_combo - 0x4faea2 - 5);
	patch_mem_u32(0x4faec2, (uintptr_t)setup_fullscreen_combo - 0x4faec1 - 5);

	if (pw_wait_for_win() == 0) {
		MessageBox(NULL, "Failed to find the PW game window", "Status", MB_OK);
		return 1;
	}

	if (g_sel_fullscreen) {
		toggle_borderless_fullscreen();
	}

	/* init the input handling */
	g_orig_event_handler = (WNDPROC)SetWindowLong(g_window, GWL_WNDPROC, (LONG)event_handler);

	return 0;
}

BOOL APIENTRY
DllMain(HMODULE mod, DWORD reason, LPVOID _reserved)
{
	switch (reason) {
	case DLL_PROCESS_ATTACH: {
		DWORD tid;

		DisableThreadLibraryCalls(mod);
		CreateThread(NULL, 0, ThreadMain, NULL, 0, &tid);
		return TRUE;
	}
	case DLL_PROCESS_DETACH:
		return TRUE;
	default:
		return FALSE;
	}

	return FALSE;
}
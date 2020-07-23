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

#include "pw_api.h"

static HMODULE g_game;
static HWND g_window;
static WNDPROC g_orig_event_handler;

static void *g_base_addr;

struct app_data *g_pw_data;
void (*pw_select_target)(int id);
void (*pw_use_skill)(int skill_id, unsigned char pvp_mask, int num_targets, int *target_ids);

static void
find_pwi_game_data(void)
{
	DWORD_PTR module_base_addr = 0;
	MODULEENTRY32 module_entry;
	HANDLE snapshot;

	snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
	if (snapshot == INVALID_HANDLE_VALUE) {
		goto err;
	}

	module_entry.dwSize = sizeof(MODULEENTRY32);

	if (Module32First(snapshot, &module_entry)) {
		do {
			if (_tcsicmp(module_entry.szModule, _T("game.exe")) == 0) {
				g_game = module_entry.hModule;
				module_base_addr = (DWORD_PTR)module_entry.modBaseAddr;
				break;
			}
		} while (Module32Next(snapshot, &module_entry));
	}
	CloseHandle(snapshot);

	if (module_base_addr == 0) {
		goto err;
	}

	g_base_addr = module_base_addr;
	g_pw_data = (struct app_data *)(g_base_addr + 0x52764c);
	pw_select_target = g_base_addr + 0x1a8080;
	pw_use_skill = g_base_addr + 0x1a8a20;

	return;

err:
	MessageBox(NULL, "Failed to retrieve PWI base address", "Status", MB_OK);
}

static BOOL CALLBACK
window_enum_cb(HWND window, LPARAM _unused)
{
	int length = GetWindowTextLength(window);
	char buf[64];
	HMODULE app;

	GetWindowText(window, buf, sizeof(buf));
	app = (HMODULE) GetWindowLong(window, GWL_HINSTANCE);
	if (strcmp(buf, "Element Client") == 0 &&
			app == g_game) {
		SetWindowText(window, "PW Mirage");
		g_window = window;
		return FALSE;
	}

	return TRUE;
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

static LRESULT CALLBACK
event_handler(HWND window, UINT event, WPARAM data, LPARAM _unused)
{
	switch(event) {
	case WM_KEYDOWN:
		if (g_pw_data && g_pw_data->game && g_pw_data->game->logged_in == 2) {
			if (data== VK_TAB) {
				select_closest_mob();
				return TRUE;
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
	int i;

	/* find and init some game data */
	find_pwi_game_data();

	fprintf(stderr, "game data at %p\n", g_pw_data);

	/* wait for the game window to appear */
	for (i = 0; i < 50; i++) {
		Sleep(150);
		EnumWindows(window_enum_cb, 0);
		if (g_window) {
			break;
		}
	}
	if (g_window == 0) {
		MessageBox(NULL, "Failed to find the PWI game window", "Status", MB_OK);
		return 1;
	}

	fprintf(stderr, "window at %x\n", (unsigned long)g_window);

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

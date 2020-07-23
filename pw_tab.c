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

#include "pw_api.h"

static HMODULE g_game;
static HWND g_window;
static WNDPROC g_orig_event_handler;

static void *g_base_addr;
static bool g_fullscreen = false;

struct app_data *g_pw_data;
void (*pw_select_target)(int id);
void (*pw_use_skill)(int skill_id, unsigned char pvp_mask, int num_targets, int *target_ids);

void
patch_mem(uintptr_t addr, const char *buf, unsigned num_bytes)
{
	DWORD prevProt;
	int i;

	fprintf(stderr, "patching %d bytes at 0x%x: ", num_bytes, addr);
	for (i = 0; i < num_bytes; i++) {
		fprintf(stderr, "0x%x ", (unsigned char)buf[i]);
	}
	fprintf(stderr, "\n");

	VirtualProtect((void *)addr, num_bytes, PAGE_EXECUTE_READWRITE, &prevProt);
	memcpy((void *)addr, buf, num_bytes);
	VirtualProtect((void *)addr, num_bytes, prevProt, NULL);
}

void
patch_mem_u32(uintptr_t addr, uint32_t u32)
{
	union {
		char c[4];
		uint32_t u;
	} u;

	u.u = u32;
	patch_mem(addr, u.c, 4);
}

void
patch_mem_u16(uintptr_t addr, uint16_t u16)
{
	union {
		char c[2];
		uint32_t u;
	} u;

	u.u = u16;
	patch_mem(addr, u.c, 2);
}

static void *
trampoline(uintptr_t addr, const char *buf, unsigned num_bytes)
{
	char *code;

	code = VirtualAlloc(NULL, (num_bytes + 0xFFF + 0x10 + 0x5) & ~0xFFF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (code == NULL) {
		MessageBox(NULL, "malloc failed", "Status", MB_OK);
		return NULL;
	}

	VirtualProtect(code, num_bytes, PAGE_EXECUTE_READWRITE, NULL);

	/* put original, replaced instructions at the end */
	memcpy(code + num_bytes, (void *)addr, 5);

	/* jump to new code */
	patch_mem(addr, "\xe9", 1);
	patch_mem_u32(addr + 1, (uintptr_t)code - addr - 5);

	memcpy(code, buf, num_bytes);

	/* jump back */
	patch_mem((uintptr_t)code + num_bytes + 5, "\xe9", 1);
	patch_mem_u32((uintptr_t)code + num_bytes + 6, addr + 5 - ((uintptr_t)code + num_bytes + 5) - 5);
	return code;
}

void
u32_to_str(char *buf, uint32_t u32)
{
	union {
		char c[4];
		uint32_t u;
	} u;

	u.u = u32;
	buf[0] = u.c[0];
	buf[1] = u.c[1];
	buf[2] = u.c[2];
	buf[3] = u.c[3];
}

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
	int i;

	/* find and init some game data */
	find_pwi_game_data();

	fprintf(stderr, "game data at %p\n", g_pw_data);

	/* always set windowed mode on startup */
	patch_mem(0x4fae99, "\x29\xd2\x90", 3);
	/* WinAPI window styles at startup when windowed */
	patch_mem_u32(0x43ba7a, 0x80000000);

	/* WinAPI window styles when windowed on every win resize */
	patch_mem_u32(0x40beb5, 0x80000000);
	/* WinAPI window styles passed to AdjustWindowRectEx on every win resize.
	 * This is used to calculate exact window dimensions to be used when positioning the
	 * window to the center of the screen. */
	patch_mem_u32(0x40beac, 0x80000000);
	/* always set windowed mode in the settings menu */
	patch_mem(0x4faed4, "\xc6\xc1\x00", 3);

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

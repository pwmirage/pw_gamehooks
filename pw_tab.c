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

static HMODULE g_game;
static HWND g_window;
static WNDPROC g_orig_event_handler;

static bool g_fullscreen = false;
static bool g_sel_fullscreen = false;

struct app_data *g_pw_data = (void *)0x92764c;
void (*pw_select_target)(int id) = (void *)0x5a8080;
void (*pw_use_skill)(int skill_id, unsigned char pvp_mask, int num_targets, int *target_ids) = (void *)0x5a8a20;

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

static char g_nops[64];

void *
trampoline(uintptr_t addr, unsigned replaced_bytes, const char *buf, unsigned num_bytes)
{
	char *code;

	assert(replaced_bytes >= 5 && replaced_bytes <= 64);
	code = VirtualAlloc(NULL, (num_bytes + 0xFFF + 0x10 + replaced_bytes) & ~0xFFF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (code == NULL) {
		MessageBox(NULL, "malloc failed", "Status", MB_OK);
		return NULL;
	}

	VirtualProtect(code, num_bytes, PAGE_EXECUTE_READWRITE, NULL);

	/* put original, replaced instructions at the end */
	memcpy(code + num_bytes, (void *)addr, replaced_bytes);

	/* jump to new code */
	patch_mem(addr, "\xe9", 1);
	patch_mem_u32(addr + 1, (uintptr_t)code - addr - 5);
	if (replaced_bytes > 5) {
		patch_mem(addr + 5, g_nops, replaced_bytes - 5);
	}

	memcpy(code, buf, num_bytes);

	/* jump back */
	patch_mem((uintptr_t)code + num_bytes + replaced_bytes, "\xe9", 1);
	patch_mem_u32((uintptr_t)code + num_bytes + replaced_bytes + 1,
		addr + 5 - ((uintptr_t)code + num_bytes + replaced_bytes + 1) - 5);
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
find_pwi_game_data(void)
{
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
				break;
			}
		} while (Module32Next(snapshot, &module_entry));
	}
	CloseHandle(snapshot);

	if (g_game == 0) {
		goto err;
	}

	return;

err:
	MessageBox(NULL, "Failed to find PW HMODULE", "Status", MB_OK);
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
spawn_debug_window(void)
{
	CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
	int consoleHandleR, consoleHandleW ;
	long stdioHandle;
	FILE *fptr;

	AllocConsole();
	SetConsoleTitle("PW Debug Console");

	EnableMenuItem(GetSystemMenu(GetConsoleWindow(), FALSE), SC_CLOSE , MF_GRAYED);
	DrawMenuBar(GetConsoleWindow());

	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &consoleInfo);

	stdioHandle = (long)GetStdHandle(STD_INPUT_HANDLE);
	consoleHandleR = _open_osfhandle(stdioHandle, 0x400);
	fptr = _fdopen(consoleHandleR, "r");
	*stdin = *fptr;
	setvbuf(stdin, NULL, _IONBF, 0);

	stdioHandle = (long) GetStdHandle(STD_OUTPUT_HANDLE);
	consoleHandleW = _open_osfhandle(stdioHandle, 0x400);
	fptr = _fdopen(consoleHandleW, "w");
	*stdout = *fptr;
	setvbuf(stdout, NULL, _IONBF, 0);

	stdioHandle = (long)GetStdHandle(STD_ERROR_HANDLE);
	*stderr = *fptr;
	setvbuf(stderr, NULL, _IONBF, 0);
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

	memset(g_nops, 0x90, sizeof(g_nops));

	/* find and init some game data */
	find_pwi_game_data();

	fprintf(stderr, "game data at %p\n", g_pw_data);
	spawn_debug_window();

	/* hook into config reading on start */
	patch_mem_u32(0x40b258, (uintptr_t)read_fullscreen_opt - 0x40b257 - 5);
	patch_mem_u32(0x40b843, (uintptr_t)save_fullscreen_opt - 0x40b842 - 5);

	/* hook into ui change handler */
	patch_mem_u32(0x55006e, (uintptr_t)on_ui_change - 0x55006d - 0 - 5);

	/* hook into combo box selection change */
	patch_mem_u32(0x6e099c, (uintptr_t)on_combo_change - 0x6e099b - 5);

	patch_mem_u32(0x4faea3, (uintptr_t)setup_fullscreen_combo - 0x4faea2 - 5);
	patch_mem_u32(0x4faec2, (uintptr_t)setup_fullscreen_combo - 0x4faec1 - 5);

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

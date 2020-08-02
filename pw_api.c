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

#include "pw_api.h"

#include <stdio.h>
#include <windows.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <inttypes.h>
#include <stdint.h>
#include <math.h>
#include <stdbool.h>
#include <assert.h>
#include <io.h>

HMODULE g_game;
HWND g_window;

struct app_data *g_pw_data = (void *)0x92764c;
void (*pw_select_target)(int id) = (void *)0x5a8080;
void (*pw_use_skill)(int skill_id, unsigned char pvp_mask, int num_targets, int *target_ids) = (void *)0x5a8a20;
void (*pw_normal_attack)(unsigned char pvp_mask) = (void *)0x5a80c0;
struct object * __thiscall (*pw_get_object)(struct world_objects *world, int id, int alive_filter) = (void *)0x429510;
unsigned __thiscall (*pw_can_touch_target)(struct player *player, float tgt_coords[3], float tgt_radius, int touch_type, float max_melee_dist) = (void *)0x458100;
void __thiscall (*pw_on_touch)(void *unk1, unsigned unk2) = (void *)0x465140;

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

	VirtualProtect(code, num_bytes + 0x10 + replaced_bytes, PAGE_EXECUTE_READWRITE, NULL);

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
trampoline_fn(void **orig_fn, unsigned replaced_bytes, void *fn)
{
	uint32_t addr = (uintptr_t)*orig_fn;
	char orig_code[32];
	char buf[32];
	char *orig;

	memcpy(orig_code, (void *)addr, replaced_bytes);

	orig = VirtualAlloc(NULL, (replaced_bytes + 5 + 0xFFF) & ~0xFFF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (orig == NULL) {
		MessageBox(NULL, "malloc failed", "Status", MB_OK);
		return;
	}

	VirtualProtect(orig, replaced_bytes + 5, PAGE_EXECUTE_READWRITE, NULL);

	/* copy original code to a buffer */
	memcpy(orig, (void *)addr, replaced_bytes);
	/* follow it by a jump to the rest of original code */
	orig[replaced_bytes] = 0xe9;
	u32_to_str(orig + replaced_bytes + 1, (uint32_t)(uintptr_t)addr + replaced_bytes - (uintptr_t)orig - replaced_bytes - 5);

	/* patch the original code to do a jump */
	buf[0] = 0xe9;
	u32_to_str(buf + 1, (uint32_t)(uintptr_t)fn - addr - 5);
	memset(buf + 5, 0x90, replaced_bytes - 5);
	patch_mem(addr, buf, replaced_bytes);

	*orig_fn = orig;
}

void
pw_spawn_debug_window(void)
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

HMODULE
pw_find_pwi_game_data(void)
{
	MODULEENTRY32 module_entry;
	HANDLE snapshot;

	snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
	if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
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

	return g_game;
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

HWND
pw_wait_for_win(void)
{
    HICON icon;
	int i;

	/* wait for the game window to appear */
	for (i = 0; i < 50; i++) {
		Sleep(150);
		EnumWindows(window_enum_cb, 0);
		if (g_window) {
			break;
		}
	}

	fprintf(stderr, "window at %x\n", (unsigned long)g_window);

    return g_window;
}

void
pw_static_init(void)
{
	memset(g_nops, 0x90, sizeof(g_nops));
}

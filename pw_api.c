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
#include <errno.h>
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
void __thiscall (*pw_console_log)(void *ui_manager, const wchar_t *msg, unsigned argb_color) = (void *)0x553cc0;
struct object * __thiscall (*pw_get_object)(struct world_objects *world, int id, int alive_filter) = (void *)0x429510;
unsigned __thiscall (*pw_can_touch_target)(struct player *player, float tgt_coords[3], float tgt_radius, int touch_type, float max_melee_dist) = (void *)0x458100;
void __thiscall (*pw_on_touch)(void *unk1, unsigned unk2) = (void *)0x465140;
unsigned __thiscall (*pw_load_configs)(struct game_data *game, void *unk1, int unk2) = (void *)0x431f30;

void
patch_mem(uintptr_t addr, const char *buf, unsigned num_bytes)
{
	char tmp[1024];
	size_t tmplen = 0;
	DWORD prevProt;
	int i;

	for (i = 0; i < num_bytes; i++) {
		_snprintf(tmp + tmplen, max(0, sizeof(tmp) - tmplen), "0x%x ", (unsigned char)buf[i]);
	}
	pw_log("patching %d bytes at 0x%x: %s", num_bytes, addr, tmp);

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

#define LOG_MAX_LEN 2048
#define LOG_SAVED_ENTRIES_COUNT 64

static unsigned g_first_log_idx = 0;
static unsigned g_last_log_idx = 0;

struct log_entry {
	unsigned argb_color;
	wchar_t msg[LOG_MAX_LEN];
};

static struct log_entry g_saved_log[LOG_SAVED_ENTRIES_COUNT];

void
pw_populate_console_log(void)
{
	struct log_entry *entry;
	unsigned i = g_first_log_idx;

	while ((i % LOG_SAVED_ENTRIES_COUNT) != (g_last_log_idx % LOG_SAVED_ENTRIES_COUNT)) {
		entry = &g_saved_log[i % LOG_SAVED_ENTRIES_COUNT];
		pw_console_log(g_pw_data->game->ui->ui_manager, entry->msg, entry->argb_color);
		i++;
	}
}

int
pw_vlog_acolor(unsigned argb_color, const char *fmt, va_list args)
{
	wchar_t fmt_w[512];
	wchar_t *fmt_wp;
	wchar_t c;
	struct log_entry *entry;
	int rc;

	rc = snwprintf(fmt_w, sizeof(fmt_w) / sizeof(fmt_w[0]), L"%S", fmt);
	if (rc < 0) {
		return rc;
	}

	fmt_wp = fmt_w;
	while ((c = *fmt_wp++)) {
		if (c == '%' && *fmt_wp == 's') {
			/* %s requires a wchar_t*, a regular char* is %S */
			*fmt_wp++ = 'S';
		} else if (c == '\n') {
			/* hide the newline, it would render as an empty square otherwise */
			*(fmt_wp - 1) = ' ';
		}
	}

	entry = &g_saved_log[g_last_log_idx++ % LOG_SAVED_ENTRIES_COUNT];
	if ((g_last_log_idx % LOG_SAVED_ENTRIES_COUNT) == (g_first_log_idx % LOG_SAVED_ENTRIES_COUNT)) {
		/* we made a loop and will now override the "first" entry", so make
		 * the second first the new first */
		g_first_log_idx++;
	}

	entry->argb_color = argb_color;
	rc = vsnwprintf(entry->msg, LOG_MAX_LEN, fmt_w, args);
	if (rc < 0) {
		return rc;
	}

	if (!g_pw_data || !g_pw_data->game || !g_pw_data->game->ui ||
			!g_pw_data->game->ui->ui_manager) {
		return 0;
	}

	pw_console_log(g_pw_data->game->ui->ui_manager, entry->msg, argb_color);
	return 0;
}

int
pw_log_acolor(unsigned argb_color, const char *fmt, ...)
{
	va_list args;
	int rc;

	va_start(args, fmt);
	rc = pw_vlog_acolor(argb_color, fmt, args);
	va_end(args);
	return rc;
}

int
pw_log_color(unsigned rgb_color, const char *fmt, ...)
{
	unsigned argb_color = rgb_color | (0xFF << 24);
	va_list args;
	int rc;

	va_start(args, fmt);
	rc = pw_vlog_acolor(argb_color, fmt, args);
	va_end(args);
	return rc;
}

int
pw_log(const char *fmt, ...)
{
	va_list args;
	int rc;

	va_start(args, fmt);
	rc = pw_vlog_acolor(0xFFFFFFFF, fmt, args);
	va_end(args);
	return rc;
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

	pw_log("window at %x\n", (unsigned long)g_window);

    return g_window;
}

void
pw_static_init(void)
{
	memset(g_nops, 0x90, sizeof(g_nops));
}

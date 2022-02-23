/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

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

#define PW_API_DEFINE 1
#include "pw_api.h"

#include "common.h"
#include "d3d.h"

HMODULE g_game;
HWND g_window;

static void __stdcall
hooked_org_pw_log(const wchar_t *line, uint32_t color)
{
	d3d_console_argb_printf(color, "%S", line);
}

TRAMPOLINE(0x553cc0, 5, "\
		jmp 0x%x;",
		hooked_org_pw_log);

void
pw_quickbar_command_skill(int row_idx, int col_idx)
{
	void *row = *(void **)((void *)g_pw_data->game->player + row_idx * 4 + 0xb9c);
	void * __thiscall (*get_skill_fn)(void *unk, int col_idx, int do_remove) = (void *)0x481200;
	void *skill = get_skill_fn(row, col_idx, 0);

	if (skill) {
		void __thiscall (*fn)(void *) = *(void **)(*(void **)skill + 8);
		fn(skill);
	}
}

void
pw_queue_action(int action_id, int param0, int param1, int param2,
		int param3, int param4, int param5)
{
	void *unk = *(void **)(*(void **)0x926fd4 + 0x1c);

	pw_queue_action_raw(unk, action_id, param0, param1, param2,
			param3, param4, param5);
}

void
pw_vlog_acolor(uint32_t argb_color, const char *fmt, va_list args)
{
	d3d_console_argb_vprintf(argb_color, fmt, args);
}

void
pw_log_acolor(uint32_t argb_color, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	pw_vlog_acolor(argb_color, fmt, args);
	va_end(args);
}

void
pw_log_color(uint32_t rgb_color, const char *fmt, ...)
{
	uint32_t argb_color = rgb_color | (0xFF << 24);
	va_list args;

	va_start(args, fmt);
	pw_vlog_acolor(argb_color, fmt, args);
	va_end(args);
}

void
pw_log(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	pw_vlog_acolor(0xFFFFFFFF, fmt, args);
	va_end(args);
}

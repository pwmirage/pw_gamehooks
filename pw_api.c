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

#include "common.h"

HMODULE g_game;
HWND g_window;

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
		} else if (c == '%' && *fmt_wp == 'S') {
			/* make %S the wchar_t */
			*fmt_wp++ = 's';
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

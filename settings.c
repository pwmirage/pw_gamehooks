/*-
 * The MIT License
 *
 * Copyright 2020 Darek Stojaczyk
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

#include <stdbool.h>
#include <assert.h>
#include <windows.h>
#include <commctrl.h>

#include "pw_api.h"
#include "common.h"

#define MG_CB_MSG (WM_USER + 165)

typedef void (*mg_callback)(void *arg1, void *arg2);

#define UI_FREEZE_CHECKBOX 1
#define UI_HP_BAR_CHECKBOX 2
#define UI_MP_BAR_CHECKBOX 3

struct ui_thread_ctx {
	mg_callback cb;
	void *arg1;
	void *arg2;
};

static bool g_initialized;
static HWND g_settings_win;

static bool *g_show_hp_bar = (bool *)0x00927D97;
static bool *g_show_mp_bar = (bool *)0x00927D98;

void
ui_thread(mg_callback cb, void *arg1, void *arg2)
{
	struct ui_thread_ctx *ctx = malloc(sizeof(*ctx));
	assert(ctx != NULL);

	ctx->cb = cb;
	ctx->arg1 = arg1;
	ctx->arg2 = arg2;
	SendMessage(g_settings_win, MG_CB_MSG, 0, (LPARAM)ctx);
}

static BOOL CALLBACK
hwnd_set_font(HWND child, LPARAM font)
{
	SendMessage(child, WM_SETFONT, font, TRUE);
	return TRUE;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM data, LPARAM ldata);

bool
is_settings_win_visible(void)
{
	if (!g_initialized || !g_settings_win) {
		return false;
	}

	IsWindowVisible(g_settings_win);
}

void
show_settings_win(bool show)
{
	WNDCLASS wc = {0};

	if (g_initialized) {
		if (g_settings_win) {
			ShowWindow(g_settings_win, show ? SW_SHOW : SW_HIDE);
		}
		return;
	}

	wc.lpszClassName = "Custom Settings";
	wc.hInstance = g_game;
	wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
	wc.lpfnWndProc = WndProc;
	wc.hCursor = LoadCursor(0, IDC_ARROW);

	RegisterClass(&wc);
	CreateWindow(wc.lpszClassName, wc.lpszClassName,
		WS_POPUP | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
		CW_USEDEFAULT, CW_USEDEFAULT, 230, 110, g_window, 0, g_game, 0);

	g_initialized = true;
}

static void
init_gui(HWND hwnd, HINSTANCE hinst)
{
	g_settings_win = hwnd;

	CreateWindow("Static", "All changes are effective immediately",
		WS_VISIBLE | WS_CHILD | WS_GROUP | SS_LEFT,
		10, 11, 210, 15, hwnd, (HMENU)0, hinst, 0);

	CreateWindow("button", "Freeze window on focus lost",
		WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
		10, 30, 210, 15,
		hwnd, (HMENU)UI_FREEZE_CHECKBOX, hinst, NULL);
	CheckDlgButton(hwnd, UI_FREEZE_CHECKBOX, BST_CHECKED);

	CreateWindow("button", "Show HP bar above player's head",
		WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
		10, 45, 210, 15,
		hwnd, (HMENU)UI_HP_BAR_CHECKBOX, hinst, NULL);
	CheckDlgButton(hwnd, UI_HP_BAR_CHECKBOX, *g_show_hp_bar ? BST_CHECKED : BST_UNCHECKED);

	CreateWindow("button", "Show MP bar above player's head",
		WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
		10, 60, 210, 15,
		hwnd, (HMENU)UI_MP_BAR_CHECKBOX, hinst, NULL);
	CheckDlgButton(hwnd, UI_MP_BAR_CHECKBOX, *g_show_mp_bar ? BST_CHECKED : BST_UNCHECKED);

	EnumChildWindows(hwnd, (WNDENUMPROC)hwnd_set_font,
		(LPARAM)GetStockObject(DEFAULT_GUI_FONT));
}

static LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM data, LPARAM ldata)
{
	switch (msg) {
	case WM_CREATE:
		init_gui(hwnd, ((LPCREATESTRUCT)ldata)->hInstance);
		SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE);
		break;
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	case WM_COMMAND: {
		int checkbox_id = LOWORD(data);
		bool check = !IsDlgButtonChecked(hwnd, checkbox_id);
		CheckDlgButton(hwnd, checkbox_id, check ? BST_CHECKED : BST_UNCHECKED);
		switch (checkbox_id) {
			case UI_FREEZE_CHECKBOX:
				patch_mem(0x42ba47, check ? "\x0f\x95\xc0" : "\xc6\xc0\x01", 3);
				break;
			case UI_HP_BAR_CHECKBOX:
				*g_show_hp_bar = check;
				break;
			case UI_MP_BAR_CHECKBOX:
				*g_show_mp_bar = check;
				break;
		}
		break;
	}
	case WM_CLOSE:
		ShowWindow(g_settings_win, SW_HIDE);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}

	return DefWindowProc(hwnd, msg, data, ldata);
}

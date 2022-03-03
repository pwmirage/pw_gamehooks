/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
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
#include <wingdi.h>

#include "pw_api.h"
#include "common.h"
#include "d3d.h"
#include "csh.h"
#include "avl.h"
#include "pw_item_desc.h"
#include "extlib.h"
#include "idmap.h"

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

static struct {
	int r_x;
	int r_y;
	int r_width;
	int r_height;
	bool r_fullscreen;
	bool r_borderless;
} g_cfg;

CSH_REGISTER_VAR_I("r_x", &g_cfg.r_x);
CSH_REGISTER_VAR_I("r_y", &g_cfg.r_y);
CSH_REGISTER_VAR_I("r_width", &g_cfg.r_width, 1024);
CSH_REGISTER_VAR_I("r_height", &g_cfg.r_height, 768);
CSH_REGISTER_VAR_B("r_fullscreen", &g_cfg.r_fullscreen);
CSH_REGISTER_VAR_B("r_borderless", &g_cfg.r_borderless);

struct rect {
	int x, y, w, h;
};

static struct rect g_window_size;
WNDPROC g_orig_event_handler;

static void
set_fullscreen_cb(void *arg1, void *arg2)
{
    RECT rect;

	if (g_window_size.w == 0 || g_cfg.r_fullscreen) {
		/* save window position & dimensions */
		GetWindowRect(g_window, &rect);
		g_window_size.x = rect.left;
		g_window_size.y = rect.top;
		g_window_size.w = rect.right - rect.left;
		g_window_size.h = rect.bottom - rect.top;
	}

	if (g_cfg.r_fullscreen) {
		int fw, fh;

		fw = GetSystemMetrics(SM_CXSCREEN);
		fh = GetSystemMetrics(SM_CYSCREEN);

		/* WinAPI window styles when windowed on every win resize -> PW sets them on every resize */
		SetWindowPos(g_window, HWND_TOP, 0, 0, fw, fh, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
	} else {
		SetWindowPos(g_window, HWND_TOP,
				g_window_size.x, g_window_size.y,
				g_window_size.w, g_window_size.h,
				SWP_SHOWWINDOW | SWP_FRAMECHANGED);
	}
}

CSH_REGISTER_VAR_CALLBACK("r_fullscreen")(void)
{
    if (g_window) {
        pw_ui_thread_postmsg(set_fullscreen_cb, NULL, NULL);
    }
}

static void
set_borderless_cb(void *arg1, void *arg2)
{
    static RECT size_w_borders;
    int w, h, x, y, fw, fh;

    unsigned style = g_cfg.r_borderless ? 0x80000000 : 0x80ce0000;
    patch_mem_u32(0x40beb5, style);
    patch_mem_u32(0x40beac, style);

    fw = GetSystemMetrics(SM_CXSCREEN);
    fh = GetSystemMetrics(SM_CYSCREEN);
    GetWindowRect(g_window, &size_w_borders);

    w = size_w_borders.right - size_w_borders.left;
    h = size_w_borders.bottom - size_w_borders.top;
    x = size_w_borders.left;
    y = size_w_borders.top;

    /* snap to screen dimensions */
    int margin = 10;
    if (abs(x) < margin) {
        x = 0;
    }

    if (abs(y) < margin) {
        y = 0;
    }

    if (abs(fw - (w + x)) < margin) {
        w = fw - x;
    }

    if (abs(fh - (h + y)) < margin) {
        h = fh - y;
    }

    SetWindowLong(g_window, GWL_STYLE, style);
    SetWindowPos(g_window, NULL, x, y, w, h, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
}

CSH_REGISTER_VAR_CALLBACK("r_borderless")(void)
{
    if (g_window) {
        pw_ui_thread_postmsg(set_borderless_cb, NULL, NULL);
    }
}

enum {
    DIM_CHANGE_NONE = 0,
    DIM_CHANGE_X,
    DIM_CHANGE_Y,
    DIM_CHANGE_WIDTH,
    DIM_CHANGE_HEIGHT,
};

static void
window_size_pos_changed_cb(void *_idchanged, void *arg2)
{
    RECT size_w_borders;
    int w, h, x, y, fw, fh;
    int bw, bh;
    int idchanged = (int)(intptr_t)_idchanged;

    GetWindowRect(g_window, &size_w_borders);

    x = size_w_borders.left;
    y = size_w_borders.top;
    w = size_w_borders.right - size_w_borders.left;
    h = size_w_borders.bottom - size_w_borders.top;

    bw = size_w_borders.right - size_w_borders.left;
    bh = size_w_borders.bottom - size_w_borders.top;

    AdjustWindowRectEx(&size_w_borders, GetWindowLong(g_window, GWL_STYLE), false,
            GetWindowLong(g_window, GWL_EXSTYLE));

    bw = size_w_borders.right - size_w_borders.left - bw;
    bh = size_w_borders.bottom - size_w_borders.top - bh;

    switch (idchanged) {
    case DIM_CHANGE_X:
        x = csh_get_i("r_x");
        break;
    case DIM_CHANGE_Y:
        y = csh_get_i("r_y");
        break;
    case DIM_CHANGE_WIDTH:
        w = csh_get_i("r_width") + bw;
        break;
    case DIM_CHANGE_HEIGHT:
        h = csh_get_i("r_height") + bh;
        break;
    default:
        break;
    }

    SetWindowPos(g_window, NULL, x, y, w, h, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
}

static void
on_window_size_pos_changed(int id)
{
    if (g_window) {
        pw_ui_thread_postmsg(window_size_pos_changed_cb, (void *)(intptr_t)id, NULL);
    }
}

CSH_REGISTER_VAR_CALLBACK("r_x")(void) { on_window_size_pos_changed(DIM_CHANGE_X); }
CSH_REGISTER_VAR_CALLBACK("r_y")(void) { on_window_size_pos_changed(DIM_CHANGE_Y); }
CSH_REGISTER_VAR_CALLBACK("r_width")(void) { on_window_size_pos_changed(DIM_CHANGE_WIDTH); }
CSH_REGISTER_VAR_CALLBACK("r_height")(void) { on_window_size_pos_changed(DIM_CHANGE_HEIGHT); }

/* don't alter window sizing; don't force 16:9 */
PATCH_MEM(0x42bb92, 2, "nop; nop");

/* set minimal window dimension 800x468 */
TRAMPOLINE(0x42bd0c, 6,
        "mov dword ptr [eax + 0x18], 800;"
        "mov dword ptr [eax + 0x1c], 468;");

static bool g_border_size_set;

static void __cdecl
hooked_on_window_resize(int w, int h)
{
    g_cfg.r_width = w;
    g_cfg.r_height = h;

    /* reset it, either because of borderless change on fullscreen switch */
    g_border_size_set = false;
}

TRAMPOLINE(0x42b918, 6,
    "push edx;"
    "push ecx;"
    "push eax;"
    "call 0x%x;"
    "pop eax;"
    "pop ecx;"
    "pop edx;"
    "call org;",
    hooked_on_window_resize);

static void __stdcall
hooked_on_windows_move(uint32_t lparam)
{
    static int bw, bh;
    static int *shadow_x, *shadow_y;
    int16_t x = lparam & 0xffff;
    int16_t y = lparam >> 16;

    if (!shadow_x) {
        shadow_x = mem_region_get_i32("_shadow_r_x");
		shadow_y = mem_region_get_i32("_shadow_r_y");
	}

    if (g_window && !g_border_size_set) {
        RECT size_w_borders;
        GetWindowRect(g_window, &size_w_borders);

        bw = x - size_w_borders.left;
        bh = y - size_w_borders.top;
        g_border_size_set = true;
    }

	g_cfg.r_x = x - bw;
	g_cfg.r_y = y - bh;
	*shadow_x = g_cfg.r_x;
	*shadow_y = g_cfg.r_y;
}

static void __attribute__((naked))
hooked_on_window_move(void)
{
    __asm__(
        "mov ebx, %0;"
        "push dword ptr [esp + 0xe4];"
        "call ebx;"
        "jmp 0x42bb94;"
        : :"r"(hooked_on_windows_move));
}

PATCH_MEM(0x42bd40, 4, ".4byte 0x%x", hooked_on_window_move);

static void __stdcall
setup_fullscreen_combo(void *unk1, void *unk2, unsigned *is_fullscreen)
{
	unsigned __stdcall (*real_fn)(void *, void *, unsigned *) = (void *)0x6d5ba0;

	if (g_cfg.r_borderless) {
		*is_fullscreen = g_cfg.r_fullscreen;
	}

	real_fn(unk1, unk2, is_fullscreen);

	if (g_cfg.r_borderless) {
		*is_fullscreen = 0;
	}
}

PATCH_JMP32(0x4faea2, setup_fullscreen_combo);
PATCH_JMP32(0x4faec1, setup_fullscreen_combo);

static int __thiscall
hooked_read_local_cfg_opt(void *unk1, const char *section, const char *name, int def_val)
{
	int ret = pw_read_local_cfg_opt(unk1, section, name, def_val);

	if (strcmp(section, "Video") == 0) {
		if (strcmp(name, "RenderWid") == 0) {
			return g_cfg.r_width;
		} else if (strcmp(name, "RenderHei") == 0) {
			return g_cfg.r_height;
		} else if (strcmp(name, "FullScreen") == 0) {
			if (g_cfg.r_borderless) {
				return 0;
			} else {
				return g_cfg.r_fullscreen;
			}
		}
	}

	return ret;
}

TRAMPOLINE_FN(&pw_read_local_cfg_opt, 5, hooked_read_local_cfg_opt);

static bool __thiscall
hooked_save_local_cfg_opt(void *unk1, const char *section, const char *name, int val)
{
	bool ret = pw_save_local_cfg_opt(unk1, section, name, val);

	if (strcmp(name, "RenderWid") == 0) {
		csh_set_i("r_width", val);
	} else if (strcmp(name, "RenderHei") == 0) {
		csh_set_i("r_height", val);
	} else if (strcmp(name, "FullScreen") == 0) {
		if (g_cfg.r_borderless) {
			val = g_cfg.r_fullscreen;
		}
		csh_set_i("r_fullscreen", val);
	}
	return ret;
}

TRAMPOLINE_FN(&pw_save_local_cfg_opt, 8, hooked_save_local_cfg_opt);

static bool g_sel_fullscreen = false;

void
settings_on_ui_change(const char *ctrl_name, struct ui_dialog *dialog)
{
    if (strcmp(ctrl_name, "default") == 0) {
        g_sel_fullscreen = false;
    } else if (strcmp(ctrl_name, "IDCANCEL") == 0) {
        g_sel_fullscreen = g_cfg.r_fullscreen;
    } else if (strcmp(ctrl_name, "apply") == 0 || strcmp(ctrl_name, "confirm") == 0) {
        if (g_cfg.r_borderless) {
            pw_log("sel: %d, real: %d\n", g_sel_fullscreen, g_cfg.r_fullscreen);
            if (g_sel_fullscreen != g_cfg.r_fullscreen) {
                csh_set_b("r_borderless", g_sel_fullscreen);
            }
        }
    }
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

PATCH_JMP32(0x6e099b, on_combo_change);

static bool
is_mouse_over_window(int safe_margin)
{
	POINT mouse_pos;
	RECT win_pos;

	GetWindowRect(g_window, &win_pos);
	GetCursorPos(&mouse_pos);

	if (mouse_pos.x > win_pos.left + safe_margin &&
			mouse_pos.x < win_pos.right - safe_margin &&
			mouse_pos.y > win_pos.top + safe_margin &&
			mouse_pos.y < win_pos.bottom - safe_margin) {
		return true;
	}

	return false;
}

static LRESULT CALLBACK
hooked_event_handler(HWND window, UINT event, WPARAM data, LPARAM lparam)
{
	static bool alt_f4_pressed;

	switch(event) {
	case WM_SIZE:
		if (data == SIZE_MINIMIZED) {
			/* PW doesnt react to this message and keeps using CPU, so make it stop */
			g_pw_data->is_render_active = false;
			break;
		} else if (data == SIZE_RESTORED) {
			g_pw_data->is_render_active = true;
			break;
		}
		break;
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
	case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	case WM_XBUTTONUP:
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	case WM_MOUSEACTIVATE:
		if (d3d_handle_mouse(event, data, lparam)) {
			return TRUE;
		}
		break;
	case WM_SYSKEYDOWN:
	case WM_KEYDOWN:
		switch (data) {
			case VK_LWIN:
			case VK_RWIN:
				if (g_cfg.r_borderless
                        && g_cfg.r_fullscreen && GetActiveWindow() == g_window
					    && is_mouse_over_window(5)) {
					ShowWindow(g_window, SW_MINIMIZE);
					g_pw_data->is_render_active = false;
				}
				break;
			case VK_F4:
				alt_f4_pressed = true;
				break;
			default:
				break;
		}
		if (d3d_handle_keyboard(event, data, lparam)) {
			return TRUE;
		}
		break;
	case WM_SYSKEYUP:
		if (data == VK_RETURN) {
			csh_set_b_toggle("r_borderless");
		} else if (data == VK_F4) {
			alt_f4_pressed = false;
		}
		if (d3d_handle_keyboard(event, data, lparam)) {
			return TRUE;
		}
		break;
	case WM_KEYUP:
	case WM_CHAR:
		switch (data) {
			case '~':
				d3d_console_toggle();
				return true;
			default:
				break;
		}
		if (d3d_handle_keyboard(event, data, lparam)) {
			return TRUE;
		}
		break;
	case WM_DEVICECHANGE:
		if (d3d_handle_keyboard(event, data, lparam)) {
			return TRUE;
		}
		break;
	case WM_KILLFOCUS:
		if ((GetAsyncKeyState(VK_MENU) & 0x8000)
                && g_cfg.r_borderless && g_cfg.r_fullscreen
			    && is_mouse_over_window(5)) {
			ShowWindow(g_window, SW_MINIMIZE);
			g_pw_data->is_render_active = false;
		}
		break;
	case WM_SYSCOMMAND:
		switch (data) {
		case SC_MINIMIZE:
			/* PW doesnt react to this message and keeps using CPU, so make it stop */
			g_pw_data->is_render_active = false;
			break;
		case SC_RESTORE:
			g_pw_data->is_render_active = true;
			break;
		default:
			break;
		}
		break;
	case WM_MENUCHAR:
		CallWindowProc(g_orig_event_handler, window, event, data, lparam);
		/* do not beep! */
		return MNC_CLOSE << 16;
	case WM_CLOSE:
		if (!alt_f4_pressed) {
			PostQuitMessage(0);
		}
		return 0;
	case MG_CB_MSG: {
		struct thread_msg_ctx ctx, *org_ctx = (void *)lparam;

		ctx = *org_ctx;
		free(org_ctx);
		ctx.cb(ctx.arg1, ctx.arg2);
		break;
	}
	default:
		break;
	}

	/* let the game handle this key */
	return CallWindowProc(g_orig_event_handler, window, event, data, lparam);
}

void
window_reinit(void)
{
    g_orig_event_handler = *mem_region_get_u32("win_event_handler");
	assert(g_orig_event_handler);
	SetWindowLong(g_window, GWL_WNDPROC, (LONG)hooked_event_handler);
}

bool
window_hooked_init(HINSTANCE hinstance, int do_show, bool _org_is_fullscreen)
{
	int rc;
	int styles;

	int x = csh_get_i("r_x");
	int y = csh_get_i("r_y");
	int w = csh_get_i("r_width");
	int h = csh_get_i("r_height");
	bool is_fullscreen = csh_get_b("r_fullscreen");
	bool is_borderless = csh_get_b("r_borderless");

	if (w == -1 && h == -1) {
		w = *(int *)0x927d82;
		h = *(int *)0x927d86;
	} else {
		*(int *)0x927d82 = w;
		*(int *)0x927d86 = h;
	}

	if (is_fullscreen && is_borderless) {
		styles = 0x80000000;
	} else {
		styles = 0x80ce0000;
	}


	if (!is_fullscreen) {
		RECT rect = { 0, 0, w, h };
		AdjustWindowRect(&rect, styles, false);

		w = rect.right - rect.left;
		h = rect.bottom - rect.top;
		if (x == -1 && y == -1) {
			x = (GetSystemMetrics(SM_CXFULLSCREEN) - w) / 2;
			y = (GetSystemMetrics(SM_CYFULLSCREEN) - h) / 2;
		}
	} else if (x == -1 && y == -1) {
		x = 0;
		y = 0;
	}

	g_window = CreateWindowEx(0, "ElementClient Window", "PW Mirage", styles,
			x, y, w, h, NULL, NULL, hinstance, NULL);
	if (!g_window) {
		return false;
	}

	if (is_borderless) {
		patch_mem_u32(0x40beb5, 0x80000000);
		patch_mem_u32(0x40beac, 0x80000000);
	}

	/* hook into PW input handling */
	g_orig_event_handler = (WNDPROC)SetWindowLong(g_window, GWL_WNDPROC,
            (LONG)hooked_event_handler);
	*mem_region_get_u32("win_event_handler") = g_orig_event_handler;
	*mem_region_get_i32("_shadow_r_x") = x;
	*mem_region_get_i32("_shadow_r_y") = y;

	/* used by PW */
	*(HINSTANCE *)0x927f5c = hinstance;
	*(HWND *)(uintptr_t)0x927f60 = g_window;

	ShowWindow(g_window, SW_SHOW);
	UpdateWindow(g_window);
	SetForegroundWindow(g_window);

	/* force the window into foreground */
	DWORD d = 0;
	DWORD windowThreadProcessId = GetWindowThreadProcessId(GetForegroundWindow(), &d);
	DWORD currentThreadId = GetCurrentThreadId();
	AttachThreadInput(windowThreadProcessId, currentThreadId, true);
	BringWindowToTop(g_window);
	ShowWindow(g_window, SW_SHOW);
	AttachThreadInput(windowThreadProcessId,currentThreadId, false);

	return true;
}
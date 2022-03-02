/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

#include <windows.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <assert.h>
#include <ctype.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <io.h>
#include <d3d9.h>

#include "common.h"
#include "pw_api.h"
#include "d3d.h"
#include "csh.h"
#include "extlib.h"
#include "icons_fontawesome.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS 1
#include "cimgui.h"

static void *g_device = NULL;
ImFont *g_font13;

struct d3d_ptrs {
	void (*init)(void *device);
	void (*new_frame)(void);
	void (*render)(void *data);
	void (*invalidate_data)(void);
	void (*reinit_data)(void);
	void (*shutdown)(void);
};

struct d3d_ptrs g_d3d9_ptrs = {
	.init = (void *)ImGui_ImplDX9_Init,
	.new_frame = (void *)ImGui_ImplDX9_NewFrame,
	.render = (void *)ImGui_ImplDX9_RenderDrawData,
	.invalidate_data = (void *)ImGui_ImplDX9_InvalidateDeviceObjects,
	.reinit_data = (void *)ImGui_ImplDX9_CreateDeviceObjects,
	.shutdown = (void *)ImGui_ImplDX9_Shutdown
};

struct d3d_ptrs g_d3d8_ptrs = {
	.init = (void *)ImGui_ImplDX8_Init,
	.new_frame = (void *)ImGui_ImplDX8_NewFrame,
	.render = (void *)ImGui_ImplDX8_RenderDrawData,
	.invalidate_data = (void *)ImGui_ImplDX8_InvalidateDeviceObjects,
	.reinit_data = (void *)ImGui_ImplDX8_CreateDeviceObjects,
	.shutdown = (void *)ImGui_ImplDX8_Shutdown
};

struct d3d_ptrs *g_d3d_ptrs;

bool g_disable_all_overlay = false;

static bool g_show_imgui_demo;
CSH_REGISTER_VAR_B("r_imgui_demo", &g_show_imgui_demo);

static unsigned __stdcall
hooked_a3d_end_scene(void *device_d3d8)
{
	unsigned __stdcall (*real_end_scene)(PDIRECT3DDEVICE9 device) = *(void **)(*(void **)device_d3d8 + 0x8c);

	if (!g_device) {
		void *device;
		if (g_d3d_ptrs == &g_d3d8_ptrs) {
			device = device_d3d8;
		} else {
			/* pointer inside d3d8to9 structure */
			device = *(void **)(device_d3d8 + 0xc);
		}

		g_device = device;
		*mem_region_get_u32("d3d_device") = g_device;

		igCreateContext(NULL);

		ImGui_ImplWin32_Init(g_window);
		g_d3d_ptrs->init(device);
		d3d_imgui_init();
	}

	g_d3d_ptrs->new_frame();
	ImGui_ImplWin32_NewFrame();
	igNewFrame();

	if (!g_disable_all_overlay) {
		d3d_try_show_target_hp();
		d3d_try_show_settings_win();
		d3d_try_show_update_win();
		d3d_try_show_console();

		if (g_show_imgui_demo) {
			igShowDemoWindow(NULL);
		}
	}

	igGetIO()->MouseDrawCursor = false;

	igEndFrame();
	igRender();
	g_d3d_ptrs->render(igGetDrawData());

	return real_end_scene(device_d3d8);
}

extern unsigned g_target_dialog_pos_y;

static unsigned __stdcall
hooked_a3d_device_reset(void *unk1, void *unk2)
{
	unsigned __stdcall (*real_device_reset)(void *unk1, void *unk2) = *(void **)(*(void **)unk1 + 0x38);
	unsigned ret;

	g_d3d_ptrs->invalidate_data();
	ret = real_device_reset(unk1, unk2);

	g_target_dialog_pos_y = 0;
	return ret;
}

int
d3d_hook(void)
{
	if (GetModuleHandleA("d3d9.dll")) {
		g_d3d_ptrs = &g_d3d9_ptrs;
	} else {
		g_d3d_ptrs = &g_d3d8_ptrs;
	}

	/* load from memory, may be NULL */
	g_font13 = *mem_region_get_u32("d3d_font13");
	g_device = *mem_region_get_u32("d3d_device");

	_patch_mem_unsafe(0x70b1fb, "\xe8\x00\x00\x00\x00\x90", 6);
	_patch_jmp32_unsafe(0x70b1fb, (uintptr_t)hooked_a3d_end_scene);

	_patch_mem_unsafe(0x70c55d, "\x51\x50\xe8\x00\x00\x00\x00", 7);
	_patch_jmp32_unsafe(0x70c55d + 2, (uintptr_t)hooked_a3d_device_reset);

	return 0;
}

void
d3d_unhook(void)
{
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool g_mouse_activated = false;
LRESULT
d3d_handle_mouse(UINT event, WPARAM data, LPARAM lparam)
{
	if (!g_device) {
		return FALSE;
	}

	ImGui_ImplWin32_WndProcHandler(g_window, event, data, lparam);
	if (event == WM_MOUSEACTIVATE) {
		g_mouse_activated = true;
	} else if (g_mouse_activated) {
		igUpdateHoveredWindowAndCaptureFlags();
		ImGui_ImplWin32_UpdateMousePos();
		g_mouse_activated = false;
	}

	return igGetIO()->WantCaptureMouse;
}

extern bool g_settings_show;

LRESULT
d3d_handle_keyboard(UINT event, WPARAM data, LPARAM lparam)
{
	if (!g_device) {
		return FALSE;
	}

	if (g_settings_show && d3d_settings_handle_keyboard(event, data, lparam)) {
		return TRUE;
	}

	ImGui_ImplWin32_WndProcHandler(g_window, event, data, lparam);
	return igGetIO()->WantTextInput;
}

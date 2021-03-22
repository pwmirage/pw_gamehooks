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

#include <windows.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <d3d9.h>

#include "common.h"
#include "d3d.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS 1
#include "cimgui.h"

static HRESULT (__stdcall *endScene_org)(LPDIRECT3DDEVICE9 pDevice);
static HRESULT (__stdcall *Reset_org)(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
static LPDIRECT3DDEVICE9 g_device = NULL;
static HWND g_window = NULL;

struct win_settings g_settings;

static HRESULT APIENTRY
hooked_endScene(LPDIRECT3DDEVICE9 device)
{
	if (!g_device) {
		g_device = device;
		igCreateContext(NULL);

		ImGui_ImplWin32_Init(g_window);
		ImGui_ImplDX9_Init(device);

		ImGuiIO *io = igGetIO();
		ImFontAtlas_AddFontFromFileTTF(io->Fonts, "fonts/fzxh1jw.ttf", 15, NULL, NULL);

		igGetStyle()->DisplayWindowPadding = (ImVec2){ 50, 50 };
	}

	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	igNewFrame();

	if (g_settings.show) {
		igSetNextWindowSize((ImVec2){270, 135}, ImGuiCond_FirstUseEver);
		igBegin("Extra Settings", &g_settings.show, ImGuiWindowFlags_NoCollapse);
		igText("All changes are applied immediately");
		if (igCheckbox("Freeze window on focus lost", &g_settings.freeze_win)) {
			patch_mem(0x42ba47, g_settings.freeze_win ? "\x0f\x95\xc0" : "\xc6\xc0\x01", 3);
		}
		if (igCheckbox("Show HP bars above entities", &g_settings.show_hp_bars)) {
			*(bool *)0x927d97 = !!g_settings.show_hp_bars;
		}
		if (igCheckbox("Show MP bars above entities", &g_settings.show_mp_bars)) {
			*(bool *)0x927d98 = !!g_settings.show_mp_bars;
		}
		igEnd();
	}

	igShowDemoWindow(NULL);

	igGetIO()->MouseDrawCursor = false;

	igEndFrame();
	igRender();
	ImGui_ImplDX9_RenderDrawData(igGetDrawData());

	return endScene_org(g_device);
}

static HRESULT APIENTRY
hooked_Reset(LPDIRECT3DDEVICE9 device, D3DPRESENT_PARAMETERS* d3dpp)
{
	HRESULT ret;

	ImGui_ImplDX9_InvalidateDeviceObjects();
	ret = Reset_org(device, d3dpp);
	ImGui_ImplDX9_CreateDeviceObjects();

	return ret;
}

int
d3d_hook(HWND hwnd)
{
	IDirect3D9* d3d;
	LPDIRECT3DDEVICE9 dummy_dev = NULL;
	D3DPRESENT_PARAMETERS d3dpp = {};
	HRESULT rc;

	g_window = hwnd;

	d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (!d3d) {
		return rc;
	}

	d3dpp.Windowed = false;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.hDeviceWindow = hwnd;

	rc = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
			&d3dpp, &dummy_dev);
	if (rc != S_OK) {
		// retry in window mode
		d3dpp.Windowed = true;

		rc = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
				d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
				&d3dpp, &dummy_dev);
		if (rc != S_OK) {
			IDirect3D9_Release(d3d);
			return rc;
		}
	}

	endScene_org = (void *)dummy_dev->lpVtbl->EndScene;
	Reset_org = (void *)dummy_dev->lpVtbl->Reset;

	dummy_dev->lpVtbl->Release(dummy_dev);
	IDirect3D9_Release(d3d);

	trampoline_fn((void **)&endScene_org, 7, hooked_endScene);
	trampoline_fn((void **)&Reset_org, 5, hooked_Reset);
	return S_OK;
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT
d3d_handle_input(UINT event, WPARAM data, LPARAM lparam)
{
	ImGui_ImplWin32_WndProcHandler(g_window, event, data, lparam);

	switch (event) {
		case WM_LBUTTONDBLCLK:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_MBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MOUSEACTIVATE:
		case WM_MOUSEHOVER:
		case WM_MOUSEMOVE:
		case WM_MOUSEWHEEL:
		case WM_NCHITTEST:
		case WM_NCLBUTTONDBLCLK:
		case WM_NCLBUTTONDOWN:
		case WM_NCLBUTTONUP:
		case WM_NCMBUTTONDBLCLK:
		case WM_NCMBUTTONDOWN:
		case WM_NCMBUTTONUP:
		case WM_NCMOUSEHOVER:
		case WM_NCMOUSEMOVE:
		case WM_NCRBUTTONDBLCLK:
		case WM_NCRBUTTONDOWN:
		case WM_NCRBUTTONUP:
		case WM_NCXBUTTONDBLCLK:
		case WM_NCXBUTTONDOWN:
		case WM_NCXBUTTONUP:
		case WM_RBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_XBUTTONDBLCLK:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
			return igGetIO()->WantCaptureMouse;
		case WM_APPCOMMAND:
		case WM_CHAR:
		case WM_DEADCHAR:
		case WM_HOTKEY:
		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_SETFOCUS:
		case WM_SYSDEADCHAR:
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
			return igGetIO()->WantTextInput;
		default:
			break;
	}

	return FALSE;
}

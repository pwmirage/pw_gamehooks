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
#include "pw_api.h"
#include "d3d.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS 1
#include "cimgui.h"

int __cxa_guard_acquire(void *arg) { };
void __cxa_guard_release(void *arg) { };

static HRESULT (__stdcall *endScene_org)(LPDIRECT3DDEVICE9 pDevice);
static HRESULT (__stdcall *Reset_org)(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
static LPDIRECT3DDEVICE9 g_device = NULL;
static ImFont *g_font;
static ImFont *g_font13;

unsigned g_target_dialog_pos_y;

bool g_settings_show;
bool g_update_show;
bool g_use_borderless = true;

static void
show_help_marker(const char* desc)
{
	igTextDisabled("(?)");
	if (igIsItemHovered(0)) {
		igBeginTooltip();
		igPushTextWrapPos(igGetFontSize() * 35.0f);
		igTextUnformatted(desc, NULL);
		igPopTextWrapPos();
		igEndTooltip();
	}
}

static void
try_show_target_hp(void)
{
	if (!g_pw_data || !g_pw_data->game || g_pw_data->game->logged_in != 2) {
		return;
	}

	if (!g_pw_data->game->player || !g_pw_data->game->player->target_id) {
		return;
	}

	static uint32_t cached_target_id = 0;
	static struct object *cached_target = NULL;

	if (g_pw_data->game->player->target_id != cached_target_id) {
		cached_target = pw_get_object(g_pw_data->game->wobj, g_pw_data->game->player->target_id, 0);
	}

	if (!cached_target) {
		return;
	}

	if (cached_target->type != 6) {
		/* not a monster */
		return;
	}

	struct mob *mob = (void *)cached_target;
	if (mob->disappear_count > 0) {
		/* mob is dead and already disappearing */
		return;
	}

	if (!g_target_dialog_pos_y) {
		struct ui_dialog *dialog = pw_get_dialog(g_pw_data->game->ui->ui_manager, "Win_HpOther");
		g_target_dialog_pos_y = dialog->pos_x;
	}

	char buf[64];
	_snprintf(buf, sizeof(buf), "%d / %d", mob->hp, mob->max_hp);

	ImVec2 text_size;
	igCalcTextSize(&text_size, buf, buf + strlen(buf), false, 0);

	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

	bool show = true;
	ImVec2 window_pos;

	window_pos.x = g_target_dialog_pos_y + 124 - text_size.x / 2;
	window_pos.y = -3;

	igPushFont(g_font13);

	igSetNextWindowPos(window_pos, ImGuiCond_Always, (ImVec2){0, 0});
	igBegin("target_hp1", &show, window_flags);
	igTextColored((ImVec4){ 0x00, 0x00, 0x00, 0xff }, buf);
	igEnd();

	window_pos.x -= 1;
	window_pos.y -= 1;
	igSetNextWindowPos(window_pos, ImGuiCond_Always, (ImVec2){0, 0});
	igBegin("target_hp2", &show, window_flags);
	igText(buf);
	igEnd();

	igPopFont();
}

static void
try_show_settings_win(void)
{
	if (!g_settings_show) {
		return;
	}

	ImGuiViewport* viewport = igGetMainViewport();
	ImVec2 work_size = viewport->WorkSize;
	ImVec2 window_pos, window_pos_pivot;

	window_pos.x = work_size.x - 5;
	window_pos.y = work_size.y - 82;
	igSetNextWindowPos(window_pos, ImGuiCond_FirstUseEver, (ImVec2){1, 1});
	igSetNextWindowSize((ImVec2){270, 160}, ImGuiCond_Always);
	igBegin("Extra Settings", &g_settings_show, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
	igText("All changes are applied immediately");
	bool freeze_win = *(uint8_t *)0x42ba47 == 0x0f;
	if (igCheckbox("Freeze window on focus lost", &freeze_win)) {
		patch_mem(0x42ba47, freeze_win ? "\x0f\x95\xc0" : "\xc6\xc0\x01", 3);
	}
	bool show_hp = !!*(uint8_t *)0x927d97;
	if (igCheckbox("Show HP bars above entities", &show_hp)) {
		*(bool *)0x927d97 = !!show_hp;
	}
	bool show_mp = !!*(uint8_t *)0x927d98;
	if (igCheckbox("Show MP bars above entities", &show_mp)) {
		*(bool *)0x927d98 = !!show_mp;
	}
	igCheckbox("Force borderless fullscreen", &g_use_borderless);
	igSameLine(0, -1); show_help_marker("Effective on next fullscreen change");
	igEnd();
}

static DWORD __stdcall
update_cb(void *arg)
{
	STARTUPINFO si = {};
	PROCESS_INFORMATION pi = {};

	si.cb = sizeof(si);

	SetCurrentDirectory("..");
	char buf[] = "pwmirage.exe --quickupdate";
	CreateProcess(NULL, buf, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
	/* ExitProcess doesn't work, so... */
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	*(uint32_t *)0x0 = 42;
	return 0;
}

static void
try_show_update_win(void)
{
	if (g_update_show) {
		igOpenPopup("MirageUpdate", 0);
		g_update_show = false;
	}

	ImVec2 center;
	ImGuiViewport_GetCenter(&center, igGetMainViewport());
	igSetNextWindowPos(center, ImGuiCond_Appearing, (ImVec2){ 0.5f, 0.5f });

	if (igBeginPopupModal("MirageUpdate", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		igText("New PW Mirage client update is available.\nWould you like to download it now?");
		igSeparator();

		if (igButton("OK", (ImVec2){ 120, 0 })) {
			DWORD tid;
			CreateThread(NULL, 0, update_cb, NULL, 0, &tid);
		}
		igSetItemDefaultFocus();
		igSameLine(0, -1);
		if (igButton("Cancel", (ImVec2){ 120, 0 })) {
			igCloseCurrentPopup();
		}
		igEndPopup();
	}
}


static HRESULT APIENTRY
hooked_endScene(LPDIRECT3DDEVICE9 device)
{
	if (g_unloading) {
		return endScene_org(device);
	}

	if (!g_device) {
		g_device = device;
		igCreateContext(NULL);

		ImGui_ImplWin32_Init(g_window);
		ImGui_ImplDX9_Init(device);

		ImGuiIO *io = igGetIO();
		//g_font = ImFontAtlas_AddFontFromFileTTF(io->Fonts, "fonts/fzxh1jw.ttf", 15, NULL, NULL);
		g_font = ImFontAtlas_AddFontFromFileTTF(io->Fonts, "fonts/calibrib.ttf", 14, NULL, NULL);
		g_font13 = ImFontAtlas_AddFontFromFileTTF(io->Fonts, "fonts/calibrib.ttf", 12, NULL, NULL);
	}

	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	igNewFrame();

	try_show_target_hp();
	try_show_settings_win();
	try_show_update_win();

	//igShowDemoWindow(NULL);

	igGetIO()->MouseDrawCursor = false;

	igEndFrame();
	igRender();
	ImGui_ImplDX9_RenderDrawData(igGetDrawData());

	return endScene_org(device);
}

static HRESULT APIENTRY
hooked_Reset(LPDIRECT3DDEVICE9 device, D3DPRESENT_PARAMETERS* d3dpp)
{
	HRESULT ret;

	if (g_unloading) {
		return Reset_org(device, d3dpp);
	}

	ImGui_ImplDX9_InvalidateDeviceObjects();
	ret = Reset_org(device, d3dpp);
	ImGui_ImplDX9_CreateDeviceObjects();

	g_target_dialog_pos_y = 0;
	return ret;
}

int
d3d_hook()
{
	IDirect3D9* d3d;
	LPDIRECT3DDEVICE9 dummy_dev = NULL;
	D3DPRESENT_PARAMETERS d3dpp = {};
	HRESULT rc;

	d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (!d3d) {
		return rc;
	}

	d3dpp.Windowed = false;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.hDeviceWindow = g_window;

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

void
d3d_unhook(void)
{
	g_device = NULL;
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

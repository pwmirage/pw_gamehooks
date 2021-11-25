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
#include "game_config.h"
#include "extlib.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS 1
#include "cimgui.h"

static void *g_device = NULL;
static ImFont *g_font;
static ImFont *g_font13;

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
	igTextColored((ImVec4){ 0xff, 0xff, 0xff, 0xff }, buf);
	igEnd();

	igPopFont();
}

void
d3d_init_settings(int why)
{
	bool show_hp = game_config_get_int("Global", "show_hp_bar", 0);
	bool show_mp = game_config_get_int("Global", "show_mp_bar", 0);

	*(bool *)0x927d97 = !!show_hp;
	*(bool *)0x927d98 = !!show_mp;

	if (why == D3D_INIT_SETTINGS_PLAYER_LOAD) {
		return;
	}

	bool render_nofocus = game_config_get_int("Global", "render_nofocus", 0);
	patch_mem(0x42ba47, render_nofocus ? "\xc6\xc0\x01" : "\x0f\x95\xc0", 3);
	g_use_borderless = game_config_get_int("Global", "borderless_fullscreen", 1);
}

static void
try_show_settings_win(void)
{
	bool check, changed = false;

	if (!g_settings_show) {
		return;
	}

	ImGuiViewport* viewport = igGetMainViewport();
	ImVec2 work_size = viewport->WorkSize;
	ImVec2 window_pos, window_pos_pivot, window_size;

	window_pos.x = work_size.x - 5;
	window_pos.y = work_size.y - 82;
	igSetNextWindowPos(window_pos, ImGuiCond_FirstUseEver, (ImVec2){1, 1});

	window_size.x = 270;
	window_size.y = 225;
	igSetNextWindowSize(window_size, ImGuiCond_Always);

	igPushStyleVarVec2(ImGuiStyleVar_WindowPadding, (ImVec2){ 2, 2 });
	igPushStyleColorVec4(ImGuiCol_Border, (ImVec4) { 0.70, 0.44, 0.39, 1.00 });
	igBegin("Extra Settings", &g_settings_show, ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse);
	igPopStyleColor(1);
	igPopStyleVar(1);

	{
		igPushStyleVarFloat(ImGuiStyleVar_ChildRounding, 4.0f);
		igPushStyleColorVec4(ImGuiCol_Border, (ImVec4){ 0.51, 0.54, 0.58, 1.00 });
		igBeginChildStr("ChildR", (ImVec2){ 0, window_size.y - 4 }, true, ImGuiWindowFlags_None);
		igPopStyleColor(1);
		igPopStyleVar(1);

		igAlignTextToFramePadding();
		igText("Extra Settings");
		igSameLine(window_size.x - 34, -1);
		if (igButton("X", (ImVec2){ 22, 22 })) {
			g_settings_show = false;
		}

		igSeparator();

		igText("All changes are applied immediately");
		check = *(uint8_t *)0x42ba47 == 0x0f;
		if (igCheckbox("Freeze window on focus lost", &check)) {
			game_config_set_int("Global", "render_nofocus", !check);
			changed = true;
			patch_mem(0x42ba47, check ? "\x0f\x95\xc0" : "\xc6\xc0\x01", 3);
		}
		check = !!*(uint8_t *)0x927d97;
		if (igCheckbox("Show HP bars above entities", &check)) {
			game_config_set_int("Global", "show_hp_bar", check);
			changed = true;
			*(bool *)0x927d97 = !!check;
		}
		check = !!*(uint8_t *)0x927d98;
		if (igCheckbox("Show MP bars above entities", &check)) {
			game_config_set_int("Global", "show_mp_bar", check);
			changed = true;
			*(bool *)0x927d98 = !!check;
		}
		check = g_use_borderless;
		if (igCheckbox("Force borderless fullscreen", &check)) {
			game_config_set_int("Global", "borderless_fullscreen", check);
			changed = true;
			g_use_borderless = check;
		}

		igSameLine(0, -1); show_help_marker("Effective on next fullscreen change");
		if (igButton("Close", (ImVec2){ 80, 22 })) {
			g_settings_show = false;
		}

		igEndChild();
	}

	igEnd();


	if (changed) {
		game_config_save(false);
	}
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
	remove_crash_handler();
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
	igSetNextWindowPos(center, ImGuiCond_Always, (ImVec2){ 0.5f, 0.5f });

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

static void
imgui_init(void)
{
	ImGuiIO *io = igGetIO();
	g_font = ImFontAtlas_AddFontFromFileTTF(io->Fonts, "fonts/calibrib.ttf", 14, NULL, NULL);
	g_font13 = ImFontAtlas_AddFontFromFileTTF(io->Fonts, "fonts/calibrib.ttf", 12, NULL, NULL);

	ImGuiStyle *style = igGetStyle();

	style->WindowPadding = (ImVec2){ 8, 8 };
	style->WindowRounding = 5.0f;
	style->FramePadding = (ImVec2){ 4, 4 };
	style->FrameRounding = 4.0f;
	style->FrameBorderSize = 1.0f;
	style->ItemSpacing = (ImVec2){ 12, 8 };
	style->ItemInnerSpacing = (ImVec2){ 8, 6 };
	style->IndentSpacing = 25.0f;
	style->ScrollbarSize = 15.0f;
	style->ScrollbarRounding = 9.0f;
	style->GrabMinSize = 5.0f;
	style->GrabRounding = 3.0f;

	style->Colors[ImGuiCol_Text] = (ImVec4){ 0.80f, 0.80f, 0.83f, 1.00f };
	style->Colors[ImGuiCol_TextDisabled] = (ImVec4){ 0.34f, 0.33f, 0.39f, 1.00f };
	style->Colors[ImGuiCol_WindowBg] = (ImVec4){ 0.06f, 0.05f, 0.07f, 0.90f };
	style->Colors[ImGuiCol_ChildBg] = (ImVec4){ 0.07f, 0.07f, 0.09f, 0.00f };
	style->Colors[ImGuiCol_PopupBg] = (ImVec4){ 0.07f, 0.07f, 0.09f, 1.00f };
	style->Colors[ImGuiCol_Border] = (ImVec4){ 0.70, 0.47, 0.39, 1.00 };
	style->Colors[ImGuiCol_BorderShadow] = (ImVec4){ 0.92f, 0.91f, 0.88f, 0.00f };
	style->Colors[ImGuiCol_FrameBg] = (ImVec4){ 0.10f, 0.09f, 0.12f, 1.00f };
	style->Colors[ImGuiCol_FrameBgHovered] = (ImVec4){ 0.18, 0.18f, 0.18f, 1.00f };
	style->Colors[ImGuiCol_FrameBgActive] = (ImVec4){ 0.24f, 0.23f, 0.29f, 1.00f };
	style->Colors[ImGuiCol_TitleBg] = (ImVec4){ 0.10f, 0.09f, 0.12f, 1.00f };
	style->Colors[ImGuiCol_TitleBgCollapsed] = (ImVec4){ 0.12f, 0.12f, 0.12f, 1.00f };
	style->Colors[ImGuiCol_TitleBgActive] = (ImVec4){ 0.07f, 0.07f, 0.09f, 1.00f };
	style->Colors[ImGuiCol_MenuBarBg] = (ImVec4){ 0.10f, 0.09f, 0.12f, 1.00f };
	style->Colors[ImGuiCol_ScrollbarBg] = (ImVec4){ 0.10f, 0.09f, 0.12f, 1.00f };
	style->Colors[ImGuiCol_ScrollbarGrab] = (ImVec4){ 0.80f, 0.80f, 0.83f, 0.31f };
	style->Colors[ImGuiCol_ScrollbarGrabHovered] = (ImVec4){ 0.56f, 0.56f, 0.58f, 1.00f };
	style->Colors[ImGuiCol_ScrollbarGrabActive] = (ImVec4){ 0.06f, 0.05f, 0.07f, 1.00f };
	style->Colors[ImGuiCol_CheckMark] = (ImVec4){ 0.40f, 1.00f, 0.00f, 1.00f };
	style->Colors[ImGuiCol_SliderGrab] = (ImVec4){ 0.80f, 0.80f, 0.83f, 0.31f };
	style->Colors[ImGuiCol_SliderGrabActive] = (ImVec4){ 0.06f, 0.05f, 0.07f, 1.00f };
	style->Colors[ImGuiCol_Button] = (ImVec4){ 0.26f, 0.17f, 0.18f, 1.00f };
	style->Colors[ImGuiCol_ButtonHovered] = (ImVec4){ 0.33f, 0.15f, 0.13f, 1.00f };
	style->Colors[ImGuiCol_ButtonActive] = (ImVec4){ 0.37f, 0.15f, 0.13f, 1.00f };
	style->Colors[ImGuiCol_Header] = (ImVec4){ 0.10f, 0.09f, 0.12f, 1.00f };
	style->Colors[ImGuiCol_HeaderHovered] = (ImVec4){ 0.56f, 0.56f, 0.58f, 1.00f };
	style->Colors[ImGuiCol_HeaderActive] = (ImVec4){ 0.06f, 0.05f, 0.07f, 1.00f };
	style->Colors[ImGuiCol_ResizeGrip] = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.00f };
	style->Colors[ImGuiCol_ResizeGripHovered] = (ImVec4){ 0.56f, 0.56f, 0.58f, 1.00f };
	style->Colors[ImGuiCol_ResizeGripActive] = (ImVec4){ 0.06f, 0.05f, 0.07f, 1.00f };
	style->Colors[ImGuiCol_PlotLines] = (ImVec4){ 0.40f, 0.39f, 0.38f, 0.63f };
	style->Colors[ImGuiCol_PlotLinesHovered] = (ImVec4){ 0.25f, 1.00f, 0.00f, 1.00f };
	style->Colors[ImGuiCol_PlotHistogram] = (ImVec4){ 0.40f, 0.39f, 0.38f, 0.63f };
	style->Colors[ImGuiCol_PlotHistogramHovered] = (ImVec4){ 0.25f, 1.00f, 0.00f, 1.00f };
	style->Colors[ImGuiCol_TextSelectedBg] = (ImVec4){ 0.25f, 1.00f, 0.00f, 0.43f };
	style->Colors[ImGuiCol_ModalWindowDimBg] = (ImVec4){ 1.00f, 0.98f, 0.95f, 0.73f };
}


static unsigned __stdcall
hooked_a3d_end_scene(void *device_d3d8)
{
	unsigned __stdcall (*real_end_scene)(PDIRECT3DDEVICE9 device) = *(void **)(*(void **)device_d3d8 + 0x8c);

	if (g_unloading) {
		return real_end_scene(device_d3d8);
	}

	if (!g_device) {
		void *device;
		if (g_d3d_ptrs == &g_d3d8_ptrs) {
			device = device_d3d8;
		} else {
			/* pointer inside d3d8to9 structure */
			device = *(void **)(device_d3d8 + 0xc);
		}

		g_device = device;
		igCreateContext(NULL);

		ImGui_ImplWin32_Init(g_window);
		g_d3d_ptrs->init(device);
		imgui_init();
	}

	g_d3d_ptrs->new_frame();
	ImGui_ImplWin32_NewFrame();
	igNewFrame();

	try_show_target_hp();
	try_show_settings_win();
	try_show_update_win();

	//igShowDemoWindow(NULL);

	igGetIO()->MouseDrawCursor = false;

	igEndFrame();
	igRender();
	g_d3d_ptrs->render(igGetDrawData());

	return real_end_scene(device_d3d8);
}

static unsigned __stdcall
hooked_a3d_device_reset(void)
{
	unsigned unk1, unk2;
	unsigned __stdcall (*real_device_reset)(unsigned unk1, unsigned unk2);
	unsigned ret;

	__asm__(
		"mov %0, eax;"
		"mov %1, ecx;"
		"mov %2, dword ptr [edx + 0x38];"
		: "=r"(unk1), "=r"(unk2), "=r"(real_device_reset));

	g_d3d_ptrs->invalidate_data();
	ret = real_device_reset(unk1, unk2);

	g_target_dialog_pos_y = 0;
	return ret;
}

int
d3d_hook()
{
	if (game_config_get_int("Global", "d3d8", 0)) {
		g_d3d_ptrs = &g_d3d8_ptrs;
	} else {
		g_d3d_ptrs = &g_d3d9_ptrs;
	}

	patch_mem(0x70b1fb, "\xe8\x00\x00\x00\x00\x90", 6);
	patch_jmp32(0x70b1fb, (uintptr_t)hooked_a3d_end_scene);

	patch_mem(0x70c55f, "\xe8\x00\x00\x00\x00", 5);
	patch_jmp32(0x70c55f, (uintptr_t)hooked_a3d_device_reset);

	return S_OK;
}

void
d3d_unhook(void)
{
	g_d3d_ptrs->shutdown();
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT
d3d_handle_mouse(UINT event, WPARAM data, LPARAM lparam)
{
	if (!g_device || g_unloading) {
		return FALSE;
	}

	ImGui_ImplWin32_WndProcHandler(g_window, event, data, lparam);
	return igGetIO()->WantCaptureMouse;
}

LRESULT
d3d_handle_keyboard(UINT event, WPARAM data, LPARAM lparam)
{
	if (!g_device || g_unloading) {
		return FALSE;
	}

	ImGui_ImplWin32_WndProcHandler(g_window, event, data, lparam);
	return igGetIO()->WantTextInput;
}

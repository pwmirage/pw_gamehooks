/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

#include <windows.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <ctype.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <io.h>
#include <d3d9.h>

#include "common.h"
#include "d3d.h"
#include "csh.h"
#include "icons_fontawesome.h"
#include "pw_api.h"
#include "extlib.h"

#include "imgui.h"

unsigned g_target_dialog_pos_y;
bool g_update_show;

void
d3d_imgui_init(void)
{
	static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0};
	ImGuiIO &io = ImGui::GetIO();
	struct ImFontConfig config = {};

	config.MergeMode = true;
	config.GlyphMinAdvanceX = 13.0f;

	io.Fonts->AddFontFromFileTTF("data/calibrib.ttf", 14, NULL, NULL);
	io.Fonts->AddFontFromFileTTF("data/fontawesome-webfont.ttf", 14, &config, icon_ranges);

	g_font13 = io.Fonts->AddFontFromFileTTF("data/calibrib.ttf", 12, NULL, NULL);
	*mem_region_get_u32("d3d_font13") = g_font13;

	ImGuiStyle &style = ImGui::GetStyle();
	ImVec4 *colors = style.Colors;

	colors[ImGuiCol_Text] = (ImVec4){ 0.80f, 0.80f, 0.83f, 1.00f };
	colors[ImGuiCol_TextDisabled] = (ImVec4){ 0.24f, 0.23f, 0.29f, 1.00f };
	colors[ImGuiCol_WindowBg] = (ImVec4){ 0.06f, 0.05f, 0.07f, 1.00f };
	colors[ImGuiCol_ChildBg] = (ImVec4){ 0.06f, 0.05f, 0.07f, 1.00f };
	colors[ImGuiCol_PopupBg] = (ImVec4){ 0.07f, 0.07f, 0.09f, 1.00f };
	colors[ImGuiCol_Border] = (ImVec4){ 0.80f, 0.80f, 0.83f, 0.48f };
	colors[ImGuiCol_BorderShadow] = (ImVec4){ 0.92f, 0.91f, 0.88f, 0.00f };
	colors[ImGuiCol_FrameBg] = (ImVec4){ 0.10f, 0.09f, 0.12f, 1.00f };
	colors[ImGuiCol_FrameBgHovered] = (ImVec4){ 0.24f, 0.23f, 0.29f, 1.00f };
	colors[ImGuiCol_FrameBgActive] = (ImVec4){ 0.18f, 0.18f, 0.20f, 1.00f };
	colors[ImGuiCol_TitleBg] = (ImVec4){ 0.10f, 0.09f, 0.12f, 1.00f };
	colors[ImGuiCol_TitleBgCollapsed] = (ImVec4){ 0.10f, 0.09f, 0.12f, 1.00f };
	colors[ImGuiCol_TitleBgActive] = (ImVec4){ 0.07f, 0.07f, 0.09f, 1.00f };
	colors[ImGuiCol_MenuBarBg] = (ImVec4){ 0.10f, 0.09f, 0.12f, 1.00f };
	colors[ImGuiCol_ScrollbarBg] = (ImVec4){ 0.10f, 0.09f, 0.12f, 1.00f };
	colors[ImGuiCol_SliderGrab] = (ImVec4){ 0.10f, 0.10f, 0.13f, 0.31f };
	colors[ImGuiCol_SliderGrabActive] = (ImVec4){ 0.06f, 0.05f, 0.07f, 1.00f };
	colors[ImGuiCol_Button] = (ImVec4){ 0.17f, 0.16f, 0.19f, 1.00f };
	colors[ImGuiCol_ButtonHovered] = (ImVec4){ 0.20f, 0.20f, 0.24f, 1.00f };
	colors[ImGuiCol_ButtonActive] = (ImVec4){ 0.27f, 0.27f, 0.33f, 1.00f };
	colors[ImGuiCol_Header] = (ImVec4){ 0.14f, 0.13f, 0.15f, 1.00f };
	colors[ImGuiCol_HeaderHovered] = (ImVec4){ 0.21f, 0.20f, 0.23f, 1.00f };
	colors[ImGuiCol_HeaderActive] = (ImVec4){ 0.06f, 0.05f, 0.07f, 1.00f };
	colors[ImGuiCol_ResizeGrip] = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.00f };
	colors[ImGuiCol_ResizeGripHovered] = (ImVec4){ 0.56f, 0.56f, 0.58f, 1.00f };
	colors[ImGuiCol_ResizeGripActive] = (ImVec4){ 0.06f, 0.05f, 0.07f, 1.00f };
	colors[ImGuiCol_PlotLines] = (ImVec4){ 0.40f, 0.39f, 0.38f, 0.63f };
	colors[ImGuiCol_PlotLinesHovered] = (ImVec4){ 0.25f, 1.00f, 0.00f, 1.00f };
	colors[ImGuiCol_PlotHistogram] = (ImVec4){ 0.40f, 0.39f, 0.38f, 0.63f };
	colors[ImGuiCol_PlotHistogramHovered] = (ImVec4){ 0.25f, 1.00f, 0.00f, 1.00f };
	colors[ImGuiCol_TextSelectedBg] = (ImVec4){ 0.25f, 1.00f, 0.00f, 0.43f };
	colors[ImGuiCol_ModalWindowDimBg] = (ImVec4){ 1.00f, 0.98f, 0.95f, 0.73f };
	colors[ImGuiCol_ScrollbarGrab] = (ImVec4){ 0.58f, 0.57f, 0.57f, 0.31f };
	colors[ImGuiCol_ScrollbarGrabHovered] = (ImVec4){ 0.43f, 0.43f, 0.45f, 1.00f };
	colors[ImGuiCol_ScrollbarGrabActive] = (ImVec4){ 0.52f, 0.52f, 0.52f, 1.00f };
	colors[ImGuiCol_CheckMark] = (ImVec4){ 0.38f, 1.00f, 0.00f, 0.51f };
	colors[ImGuiCol_Tab] = (ImVec4){ 0.18f, 0.17f, 0.19f, 1.00f };
	colors[ImGuiCol_TabHovered] = (ImVec4){ 0.24f, 0.24f, 0.24f, 1.00f };
	colors[ImGuiCol_TabActive] = (ImVec4){ 0.30f, 0.30f, 0.30f, 1.00f };
	colors[ImGuiCol_TabUnfocused] = (ImVec4){ 0.09f, 0.10f, 0.10f, 0.97f };
	colors[ImGuiCol_TabUnfocusedActive] = (ImVec4){ 0.16f, 0.17f, 0.18f, 1.00f };
	colors[ImGuiCol_TableRowBgAlt] = (ImVec4){ 0.18f, 0.18f, 0.18f, 0.06f };
	colors[ImGuiCol_ModalWindowDimBg] = (ImVec4){0.00f, 0.00f, 0.00f, 0.80f};

	style.PopupRounding = 3;

	style.WindowPadding = (ImVec2){ 10, 10 };
	style.FramePadding  = (ImVec2){ 6, 4 };
	style.ItemSpacing   = (ImVec2){ 6, 5 };

	style.ScrollbarSize = 18;

	style.WindowBorderSize = 1;
	style.ChildBorderSize  = 1;
	style.PopupBorderSize  = 1;
	style.FrameBorderSize  = 0;

	style.WindowRounding    = 3;
	style.ChildRounding     = 3;
	style.FrameRounding     = 3;
	style.ScrollbarRounding = 2;
	style.GrabRounding      = 3;
}

void
d3d_try_show_target_hp(void)
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

	struct mob *mob = (struct mob *)cached_target;
	if (mob->disappear_count > 0) {
		/* mob is dead and already disappearing */
		return;
	}

	if (!g_target_dialog_pos_y) {
		struct ui_dialog *dialog = pw_get_dialog(g_pw_data->game->ui->ui_manager, "Win_HpOther");
		g_target_dialog_pos_y = dialog->pos_x;
	}

	char buf[64];
	snprintf(buf, sizeof(buf), "%d / %d", mob->hp, mob->max_hp);

	ImVec2 text_size = ImGui::CalcTextSize(buf, buf + strlen(buf), false, 0);
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

	bool show = true;
	ImVec2 window_pos;

	window_pos.x = g_target_dialog_pos_y + 124 - text_size.x / 2;
	window_pos.y = -5;

	ImGui::PushFont(g_font13);

	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, (ImVec2){0, 0});
	ImGui::Begin("target_hp1", &show, window_flags);
	ImGui::TextColored((ImVec4){ 0x00, 0x00, 0x00, 0xff }, buf);
	ImGui::End();

	window_pos.x -= 1;
	window_pos.y -= 1;
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, (ImVec2){0, 0});
	ImGui::Begin("target_hp2", &show, window_flags);
	ImGui::TextColored((ImVec4){ 0xff, 0xff, 0xff, 0xff }, buf);
	ImGui::End();

	ImGui::PopFont();
}

void
d3d_show_help_marker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered(0)) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc, NULL);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
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

void
d3d_try_show_update_win(void)
{
	if (g_update_show) {
		ImGui::OpenPopup("MirageUpdate", 0);
		g_update_show = false;
	}

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, (ImVec2){ 0.5f, 0.5f });

	if (ImGui::BeginPopupModal("MirageUpdate", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("New PW Mirage client update is available.\nWould you like to download it now?");
		ImGui::Separator();

		if (ImGui::Button("OK", (ImVec2){ 120, 0 })) {
			DWORD tid;
			CreateThread(NULL, 0, update_cb, NULL, 0, &tid);
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine(0, -1);
		if (ImGui::Button("Cancel", (ImVec2){ 120, 0 })) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}
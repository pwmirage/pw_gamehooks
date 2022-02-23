/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021 Darek Stojaczyk for pwmirage.com
 */

#include <windows.h>
#include <inttypes.h>
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
#include "game_config.h"
#include "icons_fontawesome.h"

#include "imgui.h"

extern bool g_use_borderless;

void
d3d_try_show_settings_win(void)
{
	bool check, changed = false;

	if (!g_settings_show) {
		return;
	}

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImVec2 work_size = viewport->WorkSize;
	ImVec2 window_pos, window_pos_pivot, window_size;

	window_pos.x = work_size.x - 5;
	window_pos.y = work_size.y - 82;
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_FirstUseEver, {1, 1});

	window_size.x = 270;
	window_size.y = 225;
	ImGui::SetNextWindowSize(window_size, ImGuiCond_FirstUseEver);

	ImGui::Begin(ICON_FA_COG "Extra Settings", &g_settings_show,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse);

	window_size = ImGui::GetWindowContentRegionMax();

	ImGui::AlignTextToFramePadding();
	ImGui::Text("Extra Settings");
	ImGui::SameLine(window_size.x - 22, -1);

	ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {2, 1});
	if (ImGui::Button(ICON_FA_TIMES, {22, 22})) {
		g_settings_show = false;
	}
	ImGui::PopStyleVar(1);
	ImGui::PopStyleColor(3);

	if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("Gameplay", NULL, ImGuiTabBarFlags_None)) {
				ImGui::BeginChild("ScrollingRegion", {0, 0}, false,
								ImGuiWindowFlags_HorizontalScrollbar);

				ImGui::Text("All changes are applied immediately");
				check = *(uint8_t *)0x42ba47 == 0x0f;
				if (ImGui::Checkbox("Freeze window on focus lost", &check)) {
					game_config_set_int("Global", "render_nofocus", !check);
					changed = true;
					patch_mem(0x42ba47, check ? "\x0f\x95\xc0" : "\xc6\xc0\x01", 3);
				}
				check = !!*(uint8_t *)0x927d97;
				if (ImGui::Checkbox("Show HP bars above entities", &check)) {
					game_config_set_int("Global", "show_hp_bar", check);
					changed = true;
					*(bool *)0x927d97 = !!check;
				}
				check = !!*(uint8_t *)0x927d98;
				if (ImGui::Checkbox("Show MP bars above entities", &check)) {
					game_config_set_int("Global", "show_mp_bar", check);
					changed = true;
					*(bool *)0x927d98 = !!check;
				}
				check = g_use_borderless;
				if (ImGui::Checkbox("Force borderless fullscreen", &check)) {
					game_config_set_int("Global", "borderless_fullscreen", check);
					changed = true;
					g_use_borderless = check;
				}

				ImGui::SameLine(0, -1);
				d3d_show_help_marker("Effective on next fullscreen change");
				if (ImGui::Button("Close", {80, 22})) {
					g_settings_show = false;
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Hotkeys", NULL, ImGuiTabBarFlags_None)) {
			ImGui::BeginChild("ScrollingRegion", {0, 0}, false,
							ImGuiWindowFlags_HorizontalScrollbar);

			ImGui::Text("This is the Broccoli tab!\nblah blah blah blah blah");

			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();


	if (changed) {
		game_config_save(false);
	}
}
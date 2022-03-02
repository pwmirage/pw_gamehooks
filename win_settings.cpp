/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021 Darek Stojaczyk for pwmirage.com
 */

#include <windows.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
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
#include "input.h"
#include "pw_api.h"

#include "imgui.h"

bool g_settings_show = true;
static int g_setting_action_id = HOTKEY_A_NONE;

static struct {
    bool r_render_nofocus;
} g_cfg;

CSH_REGISTER_VAR_B("r_render_nofocus", &g_cfg.r_render_nofocus);
CSH_REGISTER_VAR_CALLBACK("r_render_nofocus")(void) {
    if (g_cfg.r_render_nofocus) {
		patch_mem(0x42ba47, "\x0f\x95\xc0", 3);
	} else {
		patch_mem(0x42ba47, "\xc6\xc0\x01", 3);
	}
};

static bool *g_r_head_hp_bar = (bool *)0x927d97;
CSH_REGISTER_VAR_B("r_head_hp_bar", g_r_head_hp_bar);
static bool *g_r_head_mp_bar = (bool *)0x927d98;
CSH_REGISTER_VAR_B("r_head_mp_bar", g_r_head_mp_bar);

static struct {
    bool listening;
    int key;
    int key_wparam;
    bool shift;
    bool control;
    bool alt;
    char str[64];
} g_setting_action_key;

void
d3d_try_show_settings_win(void)
{
	bool check, changed = false;
    int clicked_action_id = HOTKEY_A_NONE;
    static int clicked_button_no = 0;

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
				ImGuiW_CheckboxVar("r_render_nofocus", "Freeze window on focus lost");
				ImGuiW_CheckboxVar("r_head_hp_bar", "Show HP bars above entities");
				ImGuiW_CheckboxVar("r_head_mp_bar", "Show MP bars above entities");
				ImGuiW_CheckboxVar("r_borderless", "Force borderless fullscreen");

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

			if (ImGui::BeginTable("table1", 3)) {
				for (int row = 1; row < HOTKEY_A_MAX; row++) {
					ImGui::TableNextRow();

					ImGui::TableNextColumn();
                    const char *str = mg_input_action_to_str(row);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                            + (ImGui::GetColumnWidth() - ImGui::CalcTextSize(str).x) / 2
                            - ImGui::GetScrollX());

					ImGui::Text(str);
					ImGui::TableNextColumn();
                    ImGui::PushID(row * 2);
                    if (ImGui::Button("None", ImVec2(-FLT_MIN, 0.0f))) {
                        clicked_action_id = row;
                        clicked_button_no = 0;
					}
                    ImGui::PopID();
					ImGui::TableNextColumn();
					ImGui::PushID(row * 2 + 1);
					if (ImGui::Button("None", ImVec2(ImGui::GetContentRegionAvailWidth() - 10, 0.0f))) {
                        clicked_action_id = row;
                        clicked_button_no = 1;
                    }
					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();

    if (clicked_action_id) {
        ImGui::OpenPopup("SetHotkey", 0);
        g_setting_action_id = clicked_action_id;
        g_setting_action_key.str[0] = 0;
        g_setting_action_key.listening = true;
    }

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, (ImVec2){ 0.5f, 0.5f });

	if (ImGui::BeginPopupModal("SetHotkey", NULL, ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_AlwaysAutoResize)) {

        if (g_setting_action_id == HOTKEY_A_NONE) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::Text(
                "Set %shotkey for: %s",
                clicked_button_no ? "alternative " : "",
                mg_input_action_to_str(g_setting_action_id));

		ImGui::Separator();
        ImGui::NewLine();

	    ImGui::AlignTextToFramePadding();

        if (g_setting_action_key.listening) {
            unsigned ts = GetTickCount();
            if (ts % 1200 > 600) {
                ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(0.8f, 0.8f, 0.83f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(0.5f, 0.5f, 0.5f));
            }
        }

        if (!*g_setting_action_key.str) {
            ImGui::Text("< Press a key >");
        } else {
            ImGui::Text(g_setting_action_key.str);
        }

        if (g_setting_action_key.listening) {
            ImGui::PopStyleColor();
        }


        if (!g_setting_action_key.listening) {
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionWidth() - 100 + ImGui::GetStyle().WindowPadding.x);
            if (ImGui::Button("Reset", ImVec2(100, 0))) {
                g_setting_action_key.listening = true;
            }
        }

        ImGui::NewLine();
		ImGui::Separator();

		if (ImGui::Button("OK", ImVec2(140, 0))) {
			ImGui::CloseCurrentPopup();
		}

        ImGui::SameLine();

		ImGui::SetItemDefaultFocus();

		if (ImGui::Button("Cancel", ImVec2(140, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

    if (!ImGui::IsPopupOpen("SetHotkey")) {
        g_setting_action_id = HOTKEY_A_NONE;
    }
}

bool
d3d_settings_handle_keyboard(UINT event, WPARAM wparam, LPARAM lparam)
{
    if (!g_setting_action_key.listening) {
        return FALSE;
    }

    if (event == WM_KEYDOWN || event == WM_SYSKEYDOWN) {
        int key;
        const char *keyname;

        if (wparam == VK_CAPITAL || wparam == VK_ESCAPE) {
            return TRUE;
        }

        key = mg_winevent_to_event(wparam, lparam);
        keyname = mg_input_to_str(key);

        g_setting_action_key.control = GetAsyncKeyState(VK_CONTROL) & 0x8000;
        g_setting_action_key.shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
        g_setting_action_key.alt = GetAsyncKeyState(VK_MENU) & 0x8000;
        if (key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU) {
            keyname = "";
        } else {
            g_setting_action_key.listening = false;
            g_setting_action_key.key = key;
            g_setting_action_key.key_wparam = wparam;
        }

        snprintf(g_setting_action_key.str, sizeof(g_setting_action_key.str),
                "%s%s%s%s",
                g_setting_action_key.control ? "Ctrl + " : "",
                g_setting_action_key.shift ? "Shift + " : "",
                g_setting_action_key.alt ? "Alt + " : "",
                keyname);

	} else if (event == WM_KEYUP || event == WM_SYSKEYUP) {
		if (wparam == VK_ESCAPE) {
            g_setting_action_key.listening = false;
			g_setting_action_id = HOTKEY_A_NONE;
		}
	}

	return TRUE;
}

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
#include <assert.h>
#include <ctype.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <io.h>
#include <d3d9.h>

#include "common.h"
#include "pw_api.h"
#include "d3d.h"
#include "game_config.h"
#include "extlib.h"
#include "icons_fontawesome.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS 1
#include "cimgui.h"

static void *g_device = NULL;
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

bool g_disable_all_overlay = false;
bool g_settings_show = false;
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
	window_pos.y = -5;

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

static ImColor
inl_ImColor_HSVA(float h, float s, float v, float a)
{
	ImColor color;
	ImColor_HSV(&color, h, s, v, a);
	return color;
}

static ImColor
inl_ImColor_HSV(float h, float s, float v)
{
	return inl_ImColor_HSVA(h, s, v, 1.0);
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
	igSetNextWindowSize(window_size, ImGuiCond_FirstUseEver);

	igBegin(ICON_FA_COG "Extra Settings", &g_settings_show,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse);

	igGetWindowContentRegionMax(&window_size);

	igAlignTextToFramePadding();
	igText("Extra Settings");
	igSameLine(window_size.x - 22, -1);

	igPushStyleColorVec4(ImGuiCol_Button, inl_ImColor_HSV(0, 0.6f, 0.6f).Value);
	igPushStyleColorVec4(ImGuiCol_ButtonHovered, inl_ImColor_HSV(0, 0.7f, 0.7f).Value);
	igPushStyleColorVec4(ImGuiCol_ButtonActive, inl_ImColor_HSV(0, 0.8f, 0.8f).Value);
	igPushStyleVarVec2(ImGuiStyleVar_FramePadding, (ImVec2){2, 1});
	if (igButton(ICON_FA_TIMES, (ImVec2){22, 22})) {
		g_settings_show = false;
	}
	igPopStyleVar(1);
	igPopStyleColor(3);

	if (igBeginTabBar("MainTabs", ImGuiTabBarFlags_None)) {
		if (igBeginTabItem("Gameplay", NULL, ImGuiTabBarFlags_None)) {
				igBeginChildStr("ScrollingRegion", (ImVec2){0, 0}, false,
								ImGuiWindowFlags_HorizontalScrollbar);

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

				igSameLine(0, -1);
				show_help_marker("Effective on next fullscreen change");
				if (igButton("Close", (ImVec2){80, 22})) {
					g_settings_show = false;
				}
				igEndChild();
				igEndTabItem();
		}
		if (igBeginTabItem("Hotkeys", NULL, ImGuiTabBarFlags_None)) {
			igBeginChildStr("ScrollingRegion", (ImVec2){0, 0}, false,
							ImGuiWindowFlags_HorizontalScrollbar);

			igText("This is the Broccoli tab!\nblah blah blah blah blah");

			igEndChild();
			igEndTabItem();
		}
		igEndTabBar();
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

static struct console {
	bool init;
	char cmd[256];
	struct ring_buffer *log;
	struct ring_buffer *cmds;
	struct ring_buffer *cmd_history;
	/** -1 = new line, 0 .. cmd_history_size-1 = browsing history. */
	int history_pos;
	ImGuiTextFilter *filter;
	bool auto_scroll;
	bool scroll_bottom;
} g_console;

struct log_entry {
	union {
		uint32_t argb;
		struct {
			uint8_t b;
			uint8_t g;
			uint8_t r;
			uint8_t a;
		};
	} color;
	char str[0];
};

static void
console_init(void)
{
	g_console.log = ring_buffer_alloc(512);
	assert(g_console.log);
	g_console.cmds = ring_buffer_alloc(512);
	assert(g_console.cmds);
	g_console.cmd_history = ring_buffer_alloc(512);
	assert(g_console.cmd_history);
	g_console.filter = ImGuiTextFilter_ImGuiTextFilter("");
	assert(g_console.filter);

	g_console.history_pos = -1;

	ring_buffer_push(g_console.cmds, "help");
	ring_buffer_push(g_console.cmds, "di");
	ring_buffer_push(g_console.cmds, "d");

	ring_buffer_push(g_console.cmd_history, "");

	g_console.auto_scroll = true;
	g_console.scroll_bottom = false;
	g_console.init = true;
}

void
d3d_console_argb_vprintf(uint32_t argb_color, const char* fmt, va_list args)
{
	char buf[512];
	struct log_entry *e;
	int len;

	len = vsnprintf(buf, sizeof(buf), fmt, args);
	e = calloc(1, sizeof(*e) + len + 1);
	if (!e) {
		assert(false);
	}

	e->color.argb = argb_color;
	memcpy(e->str, buf, len);

	if (!g_console.init) {
		console_init();
	}

	e = ring_buffer_push(g_console.log, e);
	free(e);
}

void
d3d_console_argb_printf(uint32_t argb_color, const char* fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	d3d_console_argb_vprintf(argb_color, fmt, args);
	va_end(args);
}

static void
console_clear(void)
{
	void *el;

	RING_BUFFER_FOREACH(g_console.log, &el) {
		free(el);
	}

	g_console.log->first_idx = g_console.log->last_idx = 0;
}

static void
console_fini(void)
{
	void *el;

	RING_BUFFER_FOREACH(g_console.cmd_history, &el) {
		free(el);
	}

	console_clear();

	free(g_console.log);
	free(g_console.cmds);
	free(g_console.cmd_history);
}

static void *
trim(char* s)
{
	char* str_end = s + strlen(s);
	while (str_end > s && str_end[-1] == ' ') {
		str_end--;
		*str_end = 0;
	}

	return s;
}

static int
console_text_cb(ImGuiInputTextCallbackData* data)
{
	switch (data->EventFlag)
	{
	case ImGuiInputTextFlags_CallbackCompletion:
		{
			// Locate beginning of current word
			const char *word_end = data->Buf + data->CursorPos;
			const char *word_start = word_end;
			while (word_start > data->Buf) {
				const char c = word_start[-1];
				if (c == ' ' || c == '\t' || c == ',' || c == ';') {
					break;
				}
				word_start--;
			}

			// Build a list of candidates
			const char *candidates[32];
			const char *cmd;
			int candidates_cnt = 0;
			RING_BUFFER_FOREACH(g_console.cmds, &cmd) {
				if (_strnicmp(cmd, word_start, (int)(word_end - word_start)) == 0) {
					candidates[candidates_cnt++] = cmd;
				}
			}

			if (candidates_cnt == 0) {
				d3d_console_argb_printf(0xFFFFFFFF, "No match for \"%.*s\"!\n", (int)(word_end - word_start), word_start);
			} else if (candidates_cnt == 1) {
				// Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
				ImGuiInputTextCallbackData_DeleteChars(data, (int)(word_start - data->Buf), (int)(word_end - word_start));
				ImGuiInputTextCallbackData_InsertChars(data, data->CursorPos, candidates[0], NULL);
				ImGuiInputTextCallbackData_InsertChars(data, data->CursorPos, " ", NULL);
			} else {
				int match_len = (int)(word_end - word_start);
				for (;;) {
					int c = 0;
					bool all_candidates_matches = true;
					for (int i = 0; i < candidates_cnt && all_candidates_matches; i++)
						if (i == 0)
							c = toupper(candidates[i][match_len]);
						else if (c == 0 || c != toupper(candidates[i][match_len]))
							all_candidates_matches = false;
					if (!all_candidates_matches)
						break;
					match_len++;
				}

				if (match_len > 0)
				{
					ImGuiInputTextCallbackData_DeleteChars(data, (int)(word_start - data->Buf), (int)(word_end - word_start));
					ImGuiInputTextCallbackData_InsertChars(data, data->CursorPos, candidates[0], candidates[0] + match_len);
				}

				// List matches
				d3d_console_argb_printf(0xFFFFFFFF, "Possible matches:\n");
				for (int i = 0; i < candidates_cnt; i++)
					d3d_console_argb_printf(0xFFFFFFFF, "- %s\n", candidates[i]);
			}

			break;
		}
	case ImGuiInputTextFlags_CallbackHistory:
		{
			const int prev_history_pos = g_console.history_pos;

			if (data->EventKey == ImGuiKey_UpArrow) {
				if (g_console.history_pos == -1) {
					g_console.history_pos = ring_buffer_count(g_console.cmd_history) - 1;
				} else if (g_console.history_pos > 0) {
					g_console.history_pos--;
					char *cmd = ring_buffer_peek(g_console.cmd_history, g_console.history_pos);
					while (g_console.history_pos > 0 && *cmd == 0) {
						g_console.history_pos--;
						cmd = ring_buffer_peek(g_console.cmd_history, g_console.history_pos);
					}
				}
			} else if (data->EventKey == ImGuiKey_DownArrow) {
				if (g_console.history_pos != -1) {
					int count = ring_buffer_count(g_console.cmd_history);

					++g_console.history_pos;

					char *cmd = ring_buffer_peek(g_console.cmd_history, g_console.history_pos);
					while (g_console.history_pos < count && *cmd == 0) {
						g_console.history_pos++;
						cmd = ring_buffer_peek(g_console.cmd_history, g_console.history_pos);
					}

					if (g_console.history_pos >= count) {
						g_console.history_pos = -1;
					}
				}
			}

			if (prev_history_pos != g_console.history_pos) {
				const char *cmd = (g_console.history_pos >= 0) ? ring_buffer_peek(g_console.cmd_history, g_console.history_pos) : "";
				ImGuiInputTextCallbackData_DeleteChars(data, 0, data->BufTextLen);
				ImGuiInputTextCallbackData_InsertChars(data, 0, cmd, NULL);
			}
		}
	}
	return 0;
}

void parse_console_cmd(const char *in, char *out, size_t outlen);

static void
console_exec_cmd(const char* cmdline)
{
	void *prev;
	char *cmd;
	char buf[512];

	d3d_console_argb_printf(0xFFFFFFFF, "# %s\n", cmdline);

	g_console.history_pos = -1;
	RING_BUFFER_FOREACH_REVERSE(g_console.cmd_history, &cmd) {
		if (_stricmp(cmd, cmdline) == 0) {
			*cmd = 0;
			break;
		}
	}

	// scroll to bottom even if auto_scroll == false
	g_console.scroll_bottom = true;

	prev = ring_buffer_push(g_console.cmd_history, strdup(cmdline));
	free(prev);

	if (_stricmp(cmdline, "clear") == 0) {
		console_clear();
		return;
	} else if (_stricmp(cmdline, "help") == 0) {
		char *cmd;
		d3d_console_argb_printf(0xFFFFFFFF, "Commands:");
		RING_BUFFER_FOREACH(g_console.cmds, &cmd) {
			d3d_console_argb_printf(0xFFFFFFFF, "- %s", cmd);
		}
		return;
	} else if (_stricmp(cmdline, "history") == 0) {
		char *cmd, i = 0;
		RING_BUFFER_FOREACH_REVERSE(g_console.cmd_history, &cmd) {
			if (i++ == 10) {
				break;
			}
			if (*cmd == 0) {
				i--;
				continue;
			}
			d3d_console_argb_printf(0xFFFFFFFF, "%3d: %s\n", i, cmd);
		}
		return;
	}

	parse_console_cmd(cmdline, buf, sizeof(buf));
	cmdline = buf;

	if (g_pw_data && g_pw_data->game && g_pw_data->game->ui &&
		g_pw_data->game->ui->ui_manager && g_pw_data->game->logged_in == 2) {
			pw_console_parse_cmd(cmdline);
			pw_console_exec_cmd(g_pw_data->game->ui->ui_manager);
	}
}

bool g_show_console = false;

void
d3d_console_toggle(void)
{
	g_show_console = !g_show_console;
}

static void
try_show_console(void)
{
	static bool copy_to_clipboard = false;

	if (!g_show_console) {
		return;
	}

	igSetNextWindowSize((ImVec2){520, 60}, ImGuiCond_FirstUseEver);
	if (!igBegin("Console", &g_show_console, 0)) {
		igEnd();
		return;
	}

	if (!g_console.init) {
		console_init();
	}

	// Reserve enough left-over height for 1 separator + 1 input text
	const float footer_height_to_reserve = igGetStyle()->ItemSpacing.y + igGetFrameHeightWithSpacing();
	igBeginChildStr("ScrollingRegion", (ImVec2){0, -footer_height_to_reserve}, false, ImGuiWindowFlags_HorizontalScrollbar);
	if (igBeginPopupContextWindow(NULL, 1))
	{
		if (igSelectableBool("Clear", false, 0, (ImVec2){0, 0})) console_clear();

		igEndPopup();
	}

	igPushStyleVarVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){4, 1}); // Tighten spacing
	if (copy_to_clipboard) {
		igLogToClipboard(-1);
	}

	struct log_entry *e;
	RING_BUFFER_FOREACH(g_console.log, &e) {
		if (!ImGuiTextFilter_PassFilter(g_console.filter, e->str, NULL)) {
			continue;
		}

		ImVec4 color = { e->color.r / 256.0, e->color.g / 256.0, e->color.b / 256.0, e->color.a / 256.0 };
		bool has_color = e->color.argb != 0xFFFFFFFF;
		if (has_color) {
			igPushStyleColorVec4(ImGuiCol_Text, color);
		}

		igTextUnformatted(e->str, NULL);

		if (has_color) {
			igPopStyleColor(1);
		}
	}

	if (copy_to_clipboard) {
		igLogFinish();
		copy_to_clipboard = false;
	}

	if (g_console.scroll_bottom || (g_console.auto_scroll && igGetScrollY() >= igGetScrollMaxY())) {
		igSetScrollHereY(1.0f);
	}
	g_console.scroll_bottom = false;

	igPopStyleVar(1);
	igEndChild();
	igSeparator();

	igAlignTextToFramePadding();
	igText(">");
	igSameLine(0, -1);

	if (igIsWindowAppearing()) {
		igSetKeyboardFocusHere(0);
	}
	igSameLine(0, -1);

	ImVec2 tmp1, tmp2;
	igGetWindowContentRegionMax(&tmp2);
	float window_win = tmp2.x;

	igSetNextItemWidth(window_win - 165);

	// Command-line
	bool reclaim_focus = false;
	ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
	if (igInputText("Filter:", g_console.cmd, sizeof(g_console.cmd), input_text_flags, &console_text_cb, NULL))
	{
		char* s = trim(g_console.cmd);
		if (*s) {
			console_exec_cmd(s);
		}
		s[0] = 0;
		reclaim_focus = true;
	}

	igSameLine(0, -1);

	ImGuiTextFilter_Draw(g_console.filter, "", 60);

	igSameLine(0, -1);

	// Options menu
	if (igBeginPopup("Options", 0)) {
		igCheckbox("Auto-scroll", &g_console.auto_scroll);
		if (igButton("Scroll bottom", (ImVec2){ 120, 0 })) {
			g_console.scroll_bottom = true;
			igCloseCurrentPopup();
		}
		if (igButton("Copy", (ImVec2){ 120, 0 })) {
			copy_to_clipboard = true;
			igCloseCurrentPopup();
		}
		igEndPopup();
	}

	// Options, Filter
	if (igButton(ICON_FA_COG, (ImVec2){ 30, 0 })) {
		igOpenPopup("Options", 0);
	}

	igSameLine(0, -1);

	// Auto-focus on window apparition
	igSetItemDefaultFocus();
	if (reclaim_focus)
		igSetKeyboardFocusHere(-1); // Auto focus previous widget

	igEnd();
}

static void
imgui_init(void)
{
	static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0};
	ImGuiIO *io = igGetIO();
	struct ImFontConfig *config = ImFontConfig_ImFontConfig();

	config->MergeMode = true;
	config->GlyphMinAdvanceX = 13.0f;

	ImFontAtlas_AddFontFromFileTTF(io->Fonts, "fonts/calibrib.ttf", 14, NULL, NULL);
	ImFontAtlas_AddFontFromFileTTF(io->Fonts, "data/fontawesome-webfont.ttf", 14, config, icon_ranges);
	ImFontConfig_destroy(config);

	g_font13 = ImFontAtlas_AddFontFromFileTTF(io->Fonts, "fonts/calibrib.ttf", 12, NULL, NULL);

	ImGuiStyle *style = igGetStyle();
	ImVec4 *colors = style->Colors;

	colors[ImGuiCol_Text] = (ImVec4){ 0.80f, 0.80f, 0.83f, 1.00f };
	colors[ImGuiCol_TextDisabled] = (ImVec4){ 0.24f, 0.23f, 0.29f, 1.00f };
	colors[ImGuiCol_WindowBg] = (ImVec4){ 0.06f, 0.05f, 0.07f, 1.00f };
	colors[ImGuiCol_ChildBg] = (ImVec4){ 0.07f, 0.07f, 0.09f, 1.00f };
	colors[ImGuiCol_PopupBg] = (ImVec4){ 0.07f, 0.07f, 0.09f, 1.00f };
	colors[ImGuiCol_Border] = (ImVec4){ 0.80f, 0.80f, 0.83f, 0.88f };
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
	colors[ImGuiCol_Header] = (ImVec4){ 0.10f, 0.09f, 0.12f, 1.00f };
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
	colors[ImGuiCol_CheckMark] = (ImVec4){ 0.38f, 1.00f, 0.00f, 0.31f };
	colors[ImGuiCol_Tab] = (ImVec4){ 0.18f, 0.17f, 0.19f, 1.00f };
	colors[ImGuiCol_TabHovered] = (ImVec4){ 0.24f, 0.24f, 0.24f, 1.00f };
	colors[ImGuiCol_TabActive] = (ImVec4){ 0.30f, 0.30f, 0.30f, 1.00f };
	colors[ImGuiCol_TabUnfocused] = (ImVec4){ 0.09f, 0.10f, 0.10f, 0.97f };
	colors[ImGuiCol_TabUnfocusedActive] = (ImVec4){ 0.16f, 0.17f, 0.18f, 1.00f };
	colors[ImGuiCol_TableRowBgAlt] = (ImVec4){ 0.18f, 0.18f, 0.18f, 0.06f };

	style->PopupRounding = 3;

	style->WindowPadding = (ImVec2){ 10, 10 };
	style->FramePadding  = (ImVec2){ 6, 4 };
	style->ItemSpacing   = (ImVec2){ 6, 5 };

	style->ScrollbarSize = 18;

	style->WindowBorderSize = 1;
	style->ChildBorderSize  = 1;
	style->PopupBorderSize  = 1;
	style->FrameBorderSize  = 0;

	style->WindowRounding    = 3;
	style->ChildRounding     = 3;
	style->FrameRounding     = 3;
	style->ScrollbarRounding = 2;
	style->GrabRounding      = 3;
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

	if (!g_disable_all_overlay) {
		try_show_target_hp();
		try_show_settings_win();
		try_show_update_win();
		try_show_console();

		//igShowDemoWindow(NULL);
	}

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
	g_show_console = false;
	g_d3d_ptrs->shutdown();
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool g_mouse_activated = false;
LRESULT
d3d_handle_mouse(UINT event, WPARAM data, LPARAM lparam)
{
	if (!g_device || g_unloading) {
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

LRESULT
d3d_handle_keyboard(UINT event, WPARAM data, LPARAM lparam)
{
	if (!g_device || g_unloading) {
		return FALSE;
	}

	ImGui_ImplWin32_WndProcHandler(g_window, event, data, lparam);
	return igGetIO()->WantTextInput;
}

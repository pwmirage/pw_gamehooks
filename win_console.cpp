/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

#include <windows.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
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

#include "imgui.h"

static struct console {
	bool init;
	char cmd[256];
	struct ring_buffer *log;
	struct ring_buffer *cmds;
	struct ring_buffer *cmd_history;
	/** -1 = new line, 0 .. cmd_history_size-1 = browsing history. */
	int history_pos;
	ImGuiTextFilter filter;
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

struct ring_buffer_sp_sc *g_render_thr_queue;

struct thread_msg_ctx {
	mg_callback cb;
	void *arg1;
	void *arg2;
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

	g_console.history_pos = -1;

	ring_buffer_push(g_console.cmds, (void *)"help");
	ring_buffer_push(g_console.cmds, (void *)"di");
	ring_buffer_push(g_console.cmds, (void *)"d");

	ring_buffer_push(g_console.cmd_history, (void *)"");

    g_render_thr_queue = ring_buffer_sp_sc_new(32);

	g_console.auto_scroll = true;
	g_console.scroll_bottom = false;
	g_console.init = true;
}

static int
render_thr_post_msg(mg_callback fn, void *arg1, void *arg2)
{
	struct thread_msg_ctx *msg = (struct thread_msg_ctx *)calloc(1, sizeof(*msg));
	int rc;

	assert(msg != NULL);
	msg->cb = fn;
	msg->arg1 = arg1;
	msg->arg2 = arg2;

	rc = ring_buffer_sp_sc_push(g_render_thr_queue, msg);
	if (rc != 0) {
		free(msg);
	}

	return rc;
}

void
d3d_console_argb_vprintf(uint32_t argb_color, const char* fmt, va_list args)
{
	char buf[512];
	struct log_entry *e;
	int len;

	len = vsnprintf(buf, sizeof(buf), fmt, args);
	e = (struct log_entry *)calloc(1, sizeof(*e) + len + 1);
	if (!e) {
		assert(false);
	}

	e->color.argb = argb_color;
	memcpy(e->str, buf, len);

	if (!g_console.init) {
		console_init();
	}

	e = (struct log_entry *)ring_buffer_push(g_console.log, e);
	free((void *)e);
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
			void *cmd_v;
            const char *cmd;
			int candidates_cnt = 0;
			RING_BUFFER_FOREACH(g_console.cmds, &cmd_v) {
                cmd = (const char *)cmd_v;
				if (_strnicmp(cmd, word_start, (int)(word_end - word_start)) == 0) {
					candidates[candidates_cnt++] = cmd;
				}
			}

			if (candidates_cnt == 0) {
				d3d_console_argb_printf(0xFFFFFFFF, "No match for \"%.*s\"!\n", (int)(word_end - word_start), word_start);
			} else if (candidates_cnt == 1) {
				// Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
				data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
				data->InsertChars(data->CursorPos, candidates[0], NULL);
				data->InsertChars(data->CursorPos, " ", NULL);
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
					data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
					data->InsertChars(data->CursorPos, candidates[0], candidates[0] + match_len);
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
					char *cmd = (char *)ring_buffer_peek(g_console.cmd_history, g_console.history_pos);
					while (g_console.history_pos > 0 && *cmd == 0) {
						g_console.history_pos--;
						cmd = (char *)ring_buffer_peek(g_console.cmd_history, g_console.history_pos);
					}
				}
			} else if (data->EventKey == ImGuiKey_DownArrow) {
				if (g_console.history_pos != -1) {
					int count = ring_buffer_count(g_console.cmd_history);

					++g_console.history_pos;

					char *cmd = (char *)ring_buffer_peek(g_console.cmd_history, g_console.history_pos);
					while (g_console.history_pos < count && *cmd == 0) {
						g_console.history_pos++;
						cmd = (char *)ring_buffer_peek(g_console.cmd_history, g_console.history_pos);
					}

					if (g_console.history_pos >= count) {
						g_console.history_pos = -1;
					}
				}
			}

			if (prev_history_pos != g_console.history_pos) {
				const char *cmd = (g_console.history_pos >= 0) ? (const char *)ring_buffer_peek(g_console.cmd_history, g_console.history_pos) : "";
				data->DeleteChars(0, data->BufTextLen);
				data->InsertChars(0, cmd, NULL);
			}
		}
	}
	return 0;
}

static void
print_cmd_response(void *_str, void *unused)
{
    const char *resp = (char *)_str;

    assert(_str != NULL);

    uint32_t color = 0xFFFFFFFF;
    if (resp[0] == '^') {
        const char *new_resp;
        color = (0xFF << 24) | strtoll(resp + 1, (char **)&new_resp, 16);
        resp = new_resp;
    }
    d3d_console_argb_printf(color, "%s\n", resp);

    free(_str);
}

static void
exec_cmd_on_game_thread(void *_cmdline, void *unusd)
{
    char *cmdline = (char *)_cmdline;
    char buf[256];
    int rc = rc;

	rc = parse_console_cmd(cmdline, buf, sizeof(buf));
	if (rc == 0 && g_pw_data && g_pw_data->game && g_pw_data->game->ui &&
		g_pw_data->game->ui->ui_manager && g_pw_data->game->logged_in == 2) {
			pw_console_parse_cmd(buf);
			pw_console_exec_cmd(g_pw_data->game->ui->ui_manager);
	} else {
        const char *resp = csh_cmd(buf);
        if (resp[0] != 0) {
            render_thr_post_msg(print_cmd_response, strdup(resp), NULL);
        }
    }
    free(cmdline);
}

static void
console_exec_cmd(const char* cmdline)
{
	void *prev;
	char *cmd;
    void *cmd_v;
	char buf[512];
    int rc;

	d3d_console_argb_printf(0xFFFFFFFF, "$ %s\n", cmdline);

	g_console.history_pos = -1;
	RING_BUFFER_FOREACH_REVERSE(g_console.cmd_history, &cmd_v) {
        cmd = (char *)cmd_v;
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
		RING_BUFFER_FOREACH(g_console.cmds, &cmd_v) {
			d3d_console_argb_printf(0xFFFFFFFF, "- %s", (char *)cmd_v);
		}
		return;
	} else if (_stricmp(cmdline, "history") == 0) {
		char *cmd, i = 0;
		RING_BUFFER_FOREACH_REVERSE(g_console.cmd_history, &cmd_v) {
            cmd = (char *)cmd_v;
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

    cmdline = strdup(cmdline);
    assert(cmdline != NULL);

    pw_game_thr_post_msg(exec_cmd_on_game_thread, (void *)cmdline, NULL);
}


bool g_show_console = true;

void
d3d_console_toggle(void)
{
	g_show_console = !g_show_console;
}

void
d3d_try_show_console(void)
{
	static bool copy_to_clipboard = false;
	struct thread_msg_ctx *msg;

	/* poll one at a time */
	msg = (struct thread_msg_ctx *)ring_buffer_sp_sc_pop(g_render_thr_queue);
	if (__builtin_expect(msg != NULL, 0)) {
		msg->cb(msg->arg1, msg->arg2);
		free(msg);
	}

	if (!g_show_console) {
		return;
	}

	ImGui::SetNextWindowSize({520, 60}, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Console", &g_show_console, 0)) {
		ImGui::End();
		return;
	}

	if (!g_console.init) {
		console_init();
	}

	// Reserve enough left-over height for 1 separator + 1 input text
	const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	ImGui::BeginChild("ScrollingRegion", (ImVec2){0, -footer_height_to_reserve}, false, ImGuiWindowFlags_HorizontalScrollbar);
	if (ImGui::BeginPopupContextWindow(NULL, 1))
	{
		if (ImGui::Selectable("Clear", false, 0, {0, 0})) console_clear();

		ImGui::EndPopup();
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4, 1}); // Tighten spacing
	if (copy_to_clipboard) {
		ImGui::LogToClipboard(-1);
	}

    void *e_v;
	struct log_entry *e;
	RING_BUFFER_FOREACH(g_console.log, &e_v) {
        e = (struct log_entry *)e_v;

		if (!g_console.filter.PassFilter(e->str, NULL)) {
			continue;
		}

		ImVec4 color = { e->color.r / 256.0f, e->color.g / 256.0f, e->color.b / 256.0f, e->color.a / 256.0f };
		bool has_color = e->color.argb != 0xFFFFFFFF;
		if (has_color) {
			ImGui::PushStyleColor(ImGuiCol_Text, color);
		}

		ImGui::TextUnformatted(e->str, NULL);

		if (has_color) {
			ImGui::PopStyleColor(1);
		}
	}

	if (copy_to_clipboard) {
		ImGui::LogFinish();
		copy_to_clipboard = false;
	}

	if (g_console.scroll_bottom || (g_console.auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
		ImGui::SetScrollHereY(1.0f);
	}
	g_console.scroll_bottom = false;

	ImGui::PopStyleVar(1);
	ImGui::EndChild();
	ImGui::Separator();

	ImGui::AlignTextToFramePadding();
	ImGui::Text(">");
	ImGui::SameLine(0, -1);

	if (ImGui::IsWindowAppearing()) {
		ImGui::SetKeyboardFocusHere(0);
	}
	ImGui::SameLine(0, -1);

	ImVec2 tmp1, tmp2;
	tmp2 = ImGui::GetWindowContentRegionMax();
	float window_win = tmp2.x;

	ImGui::SetNextItemWidth(window_win - 165);

	// Command-line
	bool reclaim_focus = false;
	ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
	if (ImGui::InputText("Filter:", g_console.cmd, sizeof(g_console.cmd), input_text_flags, &console_text_cb, NULL))
	{
		char* s = (char *)trim(g_console.cmd);
		if (*s) {
			console_exec_cmd(s);
		}
		s[0] = 0;
		reclaim_focus = true;
	}

	// Auto-focus on window apparition
	ImGui::SetItemDefaultFocus();
	if (reclaim_focus)
		ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget

	ImGui::SameLine(0, -1);

	g_console.filter.Draw("", 60);

	ImGui::SameLine(0, -1);

	// Options menu
	if (ImGui::BeginPopup("Options", 0)) {
		ImGui::Checkbox("Auto-scroll", &g_console.auto_scroll);
		if (ImGui::Button("Scroll bottom", (ImVec2){ 120, 0 })) {
			g_console.scroll_bottom = true;
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::Button("Copy", (ImVec2){ 120, 0 })) {
			copy_to_clipboard = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// Options, Filter
	if (ImGui::Button(ICON_FA_COG, (ImVec2){ 30, 0 })) {
		ImGui::OpenPopup("Options", 0);
	}

	ImGui::SameLine(0, -1);

	ImGui::End();
}

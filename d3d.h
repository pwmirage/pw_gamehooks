/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

#ifndef PW_D3D_H
#define PW_D3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "extlib.h"

struct ImFont;
extern bool g_settings_show;
extern bool g_update_show;
extern struct ImFont *g_font13;

int d3d_hook(void);
void d3d_unhook(void);
LRESULT d3d_handle_mouse(UINT event, WPARAM data, LPARAM lparam);
LRESULT d3d_handle_keyboard(UINT event, WPARAM data, LPARAM lparam);

void d3d_console_argb_vprintf(uint32_t argb_color, const char* fmt, va_list args);
void d3d_console_argb_printf(uint32_t argb_color, const char* fmt, ...);
void d3d_console_toggle(void);

void d3d_imgui_init(void);
void d3d_set_dpi_scale(float scale);
typedef void (*render_thr_cb)(void *arg1, void *arg2);
int render_thr_post_msg(render_thr_cb fn, void *arg1, void *arg2);
void d3d_show_help_marker(const char* desc);
void d3d_try_show_settings_win(void);
void d3d_try_show_console(void);
void d3d_try_show_target_hp(void);
void d3d_try_show_update_win(void);
bool d3d_settings_handle_keyboard(UINT event, WPARAM data, LPARAM lparam);

#ifdef __cplusplus
}

#define ImGuiW_LabelLeft(label) \
({ \
    ImGui::AlignTextToFramePadding(); \
    ImGui::Text(label); \
    ImGui::SameLine(); \
})

/** c++ doesn't accept char* as template arguments, so use a macro */
#define ImGuiW_CheckboxVar(varname, label) \
({ \
    static bool *ptr = NULL; \
    bool ret; \
    if (ptr == NULL) { \
        ptr = (bool *)csh_get_ptr(varname); \
        assert(ptr != NULL); \
    } \
    ret = ImGui::Checkbox(label, ptr); \
    if (ret) { \
        csh_set(varname, *ptr ? "1" : "0"); \
    } \
    ret; \
})

#define ImGuiW_InputLabel(label) \
({ \
    int px = ImGui::GetCursorPosX(); \
    ImGuiW_LabelLeft(label); \
    ImGui::SetCursorPosX(px + 69 * io.FontGlobalScale); \
	ImGui::SetNextItemWidth(150 * io.FontGlobalScale); \
})

#define ImGuiW_InputInt(ptr, label) \
({ \
    ImGuiW_InputLabel(label); \
    ImGui::InputInt("###" label, ptr); \
})

#define ImGuiW_InputFloat(ptr, label, ...) \
({ \
    ImGuiW_InputLabel(label); \
    ImGui::InputFloat("###" label, ptr, __VA_ARGS__); \
})

#define ImGuiW_InputDouble(ptr, label, ...) \
({ \
    ImGuiW_InputLabel(label); \
    ImGui::InputDouble("###" label, ptr, __VA_ARGS__); \
})

#define ImGuiW_DragFloat(ptr, label, ...) \
({ \
    ImGuiW_InputLabel(label); \
    ImGui::DragFloat("###" label, ptr, __VA_ARGS__); \
})


#define ImGuiW_InputIntVar(varname, label) \
({ \
    static int *ptr = NULL; \
    if (ptr == NULL) { \
        ptr = (int *)csh_get_ptr(varname); \
        assert(ptr != NULL); \
    } \
    ImGuiW_InputInt(ptr, label); \
    if (ret) { \
        csh_set_i(varname, *ptr); \
    } \
    ptr; \
})

#define ImGuiW_InputIntShadowVar(varname, label) \
({ \
    static int *ptr = NULL; \
    static bool had_focus = false; \
    bool has_focus, ret; \
    if (ptr == NULL) { \
        ptr = mem_region_get_i32("_shadow_" varname); \
        assert(ptr != NULL); \
    } \
    ImGuiW_InputInt(ptr, label); \
    has_focus = ImGui::IsItemFocused(); \
    ret = had_focus && !has_focus; \
    had_focus = has_focus; \
    ret; \
})

#define ImGuiW_ApplyIntShadowVar(varname) \
({ \
    static int *ptr = NULL; \
    if (ptr == NULL) { \
        ptr = mem_region_get_i32("_shadow_" varname); \
        assert(ptr != NULL); \
    } \
    csh_set_i(varname, *ptr); \
})

#define ImGuiW_InputIntShadowFocusVar(varname, label) \
({ \
    static int *ptr = NULL; \
    static bool had_focus = false; \
    bool has_focus, ret; \
    if (ptr == NULL) { \
        ptr = mem_region_get_i32("_shadow_" varname); \
        assert(ptr != NULL); \
    } \
    ImGuiW_InputInt(ptr, label); \
    has_focus = ImGui::IsItemFocused(); \
    ret = had_focus && !has_focus; \
    had_focus = has_focus; \
    if (ret) { \
        csh_set_i(varname, *ptr); \
    } \
    ret; \
})

#define ImGuiW_InputDoubleShadowFocusVar(varname, label, ...) \
({ \
    static double *ptr = NULL; \
    static bool had_focus = false; \
    bool has_focus, ret; \
    if (ptr == NULL) { \
        ptr = mem_region_get_i32("_shadow_" varname); \
        assert(ptr != NULL); \
    } \
    ImGuiW_InputDouble(ptr, label, __VA_ARGS__); \
    has_focus = ImGui::IsItemFocused(); \
    ret = had_focus && !has_focus; \
    had_focus = has_focus; \
    if (ret) { \
        csh_set_i(varname, *ptr); \
    } \
    ret; \
})
#endif

#endif /* PW_D3D_H */

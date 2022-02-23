/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

#ifndef PW_D3D_H
#define PW_D3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct ImFont;
extern bool g_settings_show;
extern bool g_update_show;
extern struct ImFont *g_font13;

#define D3D_INIT_SETTINGS_INITIAL 1
#define D3D_INIT_SETTINGS_PLAYER_LOAD 2

int d3d_hook(void);
void d3d_init_settings(int why);
void d3d_unhook(void);
LRESULT d3d_handle_mouse(UINT event, WPARAM data, LPARAM lparam);
LRESULT d3d_handle_keyboard(UINT event, WPARAM data, LPARAM lparam);

void d3d_console_argb_vprintf(uint32_t argb_color, const char* fmt, va_list args);
void d3d_console_argb_printf(uint32_t argb_color, const char* fmt, ...);
void d3d_console_toggle(void);

void d3d_imgui_init(void);
void d3d_show_help_marker(const char* desc);
void d3d_try_show_settings_win(void);
void d3d_try_show_console(void);
void d3d_try_show_target_hp(void);
void d3d_try_show_update_win(void);

#ifdef __cplusplus
}
#endif

#endif /* PW_D3D_H */

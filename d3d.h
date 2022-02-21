/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

#ifndef PW_D3D_H
#define PW_D3D_H

#include <stdint.h>

extern bool g_settings_show;
extern bool g_update_show;

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

#endif /* PW_D3D_H */

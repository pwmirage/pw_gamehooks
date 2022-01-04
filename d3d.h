/*-
 * The MIT License
 *
 * Copyright 2021 Darek Stojaczyk
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

#ifndef PW_D3D
#define PW_D3D

extern bool g_settings_show;
extern bool g_update_show;

#define D3D_INIT_SETTINGS_INITIAL 1
#define D3D_INIT_SETTINGS_PLAYER_LOAD 2

int d3d_hook();
void d3d_init_settings(int why);
void d3d_unhook(void);
LRESULT d3d_handle_mouse(UINT event, WPARAM data, LPARAM lparam);
LRESULT d3d_handle_keyboard(UINT event, WPARAM data, LPARAM lparam);

void d3d_console_argb_vprintf(uint32_t argb_color, const char* fmt, va_list args);
void d3d_console_argb_printf(uint32_t argb_color, const char* fmt, ...);
void d3d_console_toggle(void);

#endif /* PW_D3D */

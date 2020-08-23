/*-
 * The MIT License
 *
 * Copyright 2020 Darek Stojaczyk
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

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>

void patch_mem(uintptr_t addr, const char *buf, unsigned num_bytes);
void patch_mem_u32(uintptr_t addr, uint32_t u32);
void patch_mem_u16(uintptr_t addr, uint16_t u16);
void patch_jmp32(uintptr_t addr, uintptr_t fn);
void *trampoline(uintptr_t addr, unsigned replaced_bytes, const char *buf, unsigned num_bytes);
void trampoline_fn(void **orig_fn, unsigned replaced_bytes, void *fn);
void u32_to_str(char *buf, uint32_t u32);

bool is_settings_win_visible(void);
void show_settings_win(bool show);

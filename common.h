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

#ifndef PW_COMMON_H
#define PW_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>

struct ring_buffer {
    size_t size;
    size_t first_idx;
    size_t last_idx;
    size_t tmp;
    void *entries[0];
};

#define RING_BUFFER_FOREACH(ring_p, el_p) \
	for ((ring_p)->tmp = (ring_p)->first_idx; \
            ((ring_p)->tmp % (ring_p)->size) != ((ring_p)->last_idx % (ring_p)->size) \
            && (*(el_p) = (ring_p)->entries[(ring_p)->tmp % (ring_p)->size]); \
            (ring_p)->tmp++)

#define RING_BUFFER_FOREACH_REVERSE(ring_p, el_p) \
	for ((ring_p)->tmp = (ring_p)->last_idx - 1; \
            ((ring_p)->tmp % (ring_p)->size) != (((ring_p)->first_idx - 1) % (ring_p)->size) \
            && (*(el_p) = (ring_p)->entries[(ring_p)->tmp % (ring_p)->size]); \
            (ring_p)->tmp--)

void *ring_buffer_push(struct ring_buffer *ring, void *data);
size_t ring_buffer_count(struct ring_buffer *ring);
void *ring_buffer_pop(struct ring_buffer *ring);
void *ring_buffer_peek(struct ring_buffer *ring, int off);
struct ring_buffer *ring_buffer_alloc(size_t size);

void patch_mem(uintptr_t addr, const char *buf, unsigned num_bytes);
void patch_mem_u32(uintptr_t addr, uint32_t u32);
void patch_mem_u16(uintptr_t addr, uint16_t u16);
void patch_jmp32(uintptr_t addr, uintptr_t fn);
void trampoline_call(uintptr_t addr, unsigned replaced_bytes, void *fn);
void *trampoline_buf(uintptr_t addr, unsigned replaced_bytes, const char *buf, unsigned num_bytes);
void trampoline_fn(void **orig_fn, unsigned replaced_bytes, void *fn);
void trampoline_winapi_fn(void **orig_fn, void *fn);
void u32_to_str(char *buf, uint32_t u32);
void restore_mem(void);
int assemble_x86(uint32_t addr, const char *in, unsigned char **out);

/* patch executable memory without making backups */
void _patch_mem_unsafe(uintptr_t addr, const char *buf, unsigned num_bytes);
void _patch_mem_u32_unsafe(uintptr_t addr, uint32_t u32);
void _patch_jmp32_unsafe(uintptr_t addr, uintptr_t fn);

void common_static_init(void);
void common_static_fini(void);

void trampoline_fn_static_add(void **orig_fn, int replaced_bytes, void *fn);
void trampoline_static_add(uintptr_t addr, int replaced_bytes, const char *asm_fmt, ...);
void patch_mem_static_add(uintptr_t addr, int replaced_bytes, const char *asm_fmt, ...);
void patch_mem_static_init(void);

#define _COMMON_JOIN2(a, b) a ## _ ## b
#define COMMON_JOIN2(a, b) _COMMON_JOIN2(a, b)
#define COMMON_UNIQUENAME(str) COMMON_JOIN2(str, __LINE__)
#define TRAMPOLINE_ORG "call org"
#define TRAMPOLINE(addr_p, replaced_bytes_p, ...) \
static void __attribute__((constructor)) COMMON_UNIQUENAME(init_trampoline_)(void) { \
    trampoline_static_add(addr_p, replaced_bytes_p, __VA_ARGS__); \
}

#define TRAMPOLINE_FN(fn_p, replaced_bytes_p, ...) \
static void __attribute__((constructor)) COMMON_UNIQUENAME(init_trampoline_)(void) { \
    trampoline_fn_static_add((void **)fn_p, replaced_bytes_p, __VA_ARGS__); \
}

#define PATCH_MEM(addr_p, replaced_bytes_p, ...) \
static void __attribute__((constructor)) COMMON_UNIQUENAME(init_patch_mem_)(void) { \
    patch_mem_static_add(addr_p, replaced_bytes_p, __VA_ARGS__); \
}

#define PATCH_JMP32(addr_p, fn_p) \
static void __attribute__((constructor)) COMMON_UNIQUENAME(init_patch_jmp_)(void) { \
    char tmp[16]; \
    if (*(unsigned char *)(uintptr_t)addr_p == 0xe8) { \
        _snprintf(tmp, sizeof(tmp), "call 0x%x", (uintptr_t)fn_p); \
    } else { \
        _snprintf(tmp, sizeof(tmp), "jmp 0x%x", (uintptr_t)fn_p); \
    } \
    patch_mem_static_add(addr_p, 5, tmp); \
}

#ifdef __cplusplus
}
#endif

#endif /* PW_COMMON_H */

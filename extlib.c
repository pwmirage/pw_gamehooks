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

#define PACKAGE "libgamehook.dll"
#define PACKAGE_VERSION "1.0"

#include <stddef.h>
#include <unistd.h>
#include <windows.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <d3d9.h>
#include <imagehlp.h>
#include <assert.h>
#include <bfd.h>

#include "extlib.h"
#include "avl.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS 1
#include <cimgui.h>

/* we don't need full libintl */
char * libintl_dgettext(char *domain, char *mesgid) { return NULL; };

/* linux-only stuff */
pid_t fork(void) { return 0; };

/* c++ mocks to avoid linking with libc++ which we only use
 * in one or two imgui demos */
int __cxa_guard_acquire(void *arg) { return 0; };
void __cxa_guard_release(void *arg) { };

static struct pw_avl *g_mem;

static uint32_t
djb2(const char *str)
{
	uint32_t hash = 5381;
	unsigned char c;

	while ((c = (unsigned char)*str++)) {
	    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}

	return hash;
}

static struct mem_region *
_get_mem(uint32_t hash, const char *name)
{
    struct mem_region *mem;

	mem = pw_avl_get(g_mem, hash);
	while (mem && strcmp(mem->name, name) != 0) {
		mem = pw_avl_get_next(g_mem, mem);
	}

	return mem;
}

struct mem_region *
mem_region_get(const char *name, uint32_t size)
{
	struct mem_region *mem;
    uint32_t hash = djb2(name);

    if (!g_mem) {
        return NULL;
    }

	mem = _get_mem(hash, name);
    if (mem) {
        return mem;
    }

	mem = pw_avl_alloc(g_mem);
	if (!mem) {
		return NULL;
	}

    snprintf(mem->name, sizeof(mem->name), "%s", name);
    mem->size = size;

    if (size) {
        mem->data = calloc(1, size);
        if (!mem->data) {
            pw_avl_free(g_mem, mem);
            return NULL;
        }
    }

    pw_avl_insert(g_mem, hash, mem);
    return mem;
}

void **
mem_region_get_u32(const char *name)
{
    struct mem_region *mem;

    mem = mem_region_get(name, 0);
    assert(mem);
    if (mem->size != 0) {
        mem_region_free(mem);
    }

    mem = mem_region_get(name, 0);
    assert(mem);

    return &mem->data;
}

int *
mem_region_get_i32(const char *name)
{
    return (int *)mem_region_get_u32(name);
}

void
mem_region_free(struct mem_region *mem)
{
    pw_avl_remove(g_mem, mem);
    pw_avl_free(g_mem, mem);
}

static void __attribute__((constructor))
extlib_init(void)
{
    g_mem = pw_avl_init(sizeof(struct mem_region));
    if (!g_mem) {
        /* run to the woods */
    }
}

static void __attribute__((destructor))
extlib_fini(void)
{
    /* TODO */
}

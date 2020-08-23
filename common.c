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

#include "common.h"

#include <stdio.h>
#include <windows.h>
#include <assert.h>
#include <math.h>
#include <io.h>

#include "pw_api.h"

struct mem_region_4kb {
	char data[4096];
	/* which bytes were overwritten */
	bool byte_mask[4096];
};

struct mem_region_1mb {
	struct mem_region_4kb *pages[256];
};

struct mem_region_1mb *g_mem_map[4096];

static void
backup_page_mem(uintptr_t addr, unsigned len)
{
	uintptr_t addr_4k = addr / 4096;
	uintptr_t addr_1mb = addr_4k / 256;
	uintptr_t offset_4k = addr_4k % 256;
	struct mem_region_1mb *reg_1m = g_mem_map[addr_1mb];
	struct mem_region_4kb *reg_4k;
	unsigned i;

	fprintf(stderr, "backing up page 0x%p at 0x%p, 1mb=%x, 4k=%x\n", addr_4k, addr_4k * 4096, addr_1mb, offset_4k);
	if (!reg_1m) {
		reg_1m = g_mem_map[addr_1mb] = calloc(1, sizeof(*reg_1m));
		if (!reg_1m) {
			pw_log("calloc() failed in %s", __func__);
			return;
		}
	}

	reg_4k = reg_1m->pages[offset_4k];
	if (!reg_4k) {
		reg_4k = reg_1m->pages[offset_4k] = calloc(1, sizeof(*reg_4k));
		if (!reg_4k) {
			pw_log("calloc() failed in %s", __func__);
			return;
		}

		memcpy(reg_4k->data, (void *)(addr_4k * 4096), 4096);
	}

	for (i = 0; i < len && i < 4096; i++) {
		reg_4k->byte_mask[addr % 4096 + i] = 1;
	}
}

static void
backup_mem(uintptr_t addr, unsigned num_bytes)
{
	unsigned u;

	for (u = 0; u < (num_bytes + 4095) / 4096; u++) {
		unsigned page_bytes = min(num_bytes, 4096 - (addr % 4096));
		backup_page_mem(addr, page_bytes);
		addr += page_bytes;
	}
}

void
patch_mem(uintptr_t addr, const char *buf, unsigned num_bytes)
{
	char tmp[1024];
	size_t tmplen = 0;
	DWORD prevProt;
	int i;

	for (i = 0; i < num_bytes; i++) {
		_snprintf(tmp + tmplen, max(0, sizeof(tmp) - tmplen), "0x%x ", (unsigned char)buf[i]);
	}
	pw_log("patching %d bytes at 0x%x: %s", num_bytes, addr, tmp);

	backup_mem(addr, num_bytes);
	VirtualProtect((void *)addr, num_bytes, PAGE_EXECUTE_READWRITE, &prevProt);
	memcpy((void *)addr, buf, num_bytes);
	VirtualProtect((void *)addr, num_bytes, prevProt, NULL);
}

void
patch_mem_u32(uintptr_t addr, uint32_t u32)
{
	union {
		char c[4];
		uint32_t u;
	} u;

	u.u = u32;
	patch_mem(addr, u.c, 4);
}

void
patch_mem_u16(uintptr_t addr, uint16_t u16)
{
	union {
		char c[2];
		uint32_t u;
	} u;

	u.u = u16;
	patch_mem(addr, u.c, 2);
}

void
patch_jmp32(uintptr_t addr, uintptr_t fn)
{
	uint8_t op = *(char *)addr;
	if (op != 0xe9 && op != 0xe8) {
		pw_log("Opcode %X at 0x%p is not a valid JMP/CALL", op, addr);
		return;
	}

	patch_mem_u32(addr + 1, fn - addr - 5);
}

void
u32_to_str(char *buf, uint32_t u32)
{
	union {
		char c[4];
		uint32_t u;
	} u;

	u.u = u32;
	buf[0] = u.c[0];
	buf[1] = u.c[1];
	buf[2] = u.c[2];
	buf[3] = u.c[3];
}

static char g_nops[64];

void *
trampoline(uintptr_t addr, unsigned replaced_bytes, const char *buf, unsigned num_bytes)
{
	char *code;

	assert(replaced_bytes >= 5 && replaced_bytes <= 64);
	code = VirtualAlloc(NULL, (num_bytes + 0xFFF + 0x10 + replaced_bytes) & ~0xFFF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (code == NULL) {
		MessageBox(NULL, "malloc failed", "Status", MB_OK);
		return NULL;
	}

	VirtualProtect(code, num_bytes + 0x10 + replaced_bytes, PAGE_EXECUTE_READWRITE, NULL);

	/* put original, replaced instructions at the end */
	memcpy(code + num_bytes, (void *)addr, replaced_bytes);

	/* jump to new code */
	patch_mem(addr, "\xe9", 1);
	patch_mem_u32(addr + 1, (uintptr_t)code - addr - 5);
	if (replaced_bytes > 5) {
		patch_mem(addr + 5, g_nops, replaced_bytes - 5);
	}

	memcpy(code, buf, num_bytes);

	/* jump back */
	patch_mem((uintptr_t)code + num_bytes + replaced_bytes, "\xe9", 1);
	patch_mem_u32((uintptr_t)code + num_bytes + replaced_bytes + 1,
		addr + 5 - ((uintptr_t)code + num_bytes + replaced_bytes + 1) - 5);
	return code;
}

void
trampoline_fn(void **orig_fn, unsigned replaced_bytes, void *fn)
{
	uint32_t addr = (uintptr_t)*orig_fn;
	char orig_code[32];
	char buf[32];
	char *orig;

	memcpy(orig_code, (void *)addr, replaced_bytes);

	orig = VirtualAlloc(NULL, (replaced_bytes + 5 + 0xFFF) & ~0xFFF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (orig == NULL) {
		MessageBox(NULL, "malloc failed", "Status", MB_OK);
		return;
	}

	VirtualProtect(orig, replaced_bytes + 5, PAGE_EXECUTE_READWRITE, NULL);

	/* copy original code to a buffer */
	memcpy(orig, (void *)addr, replaced_bytes);
	/* follow it by a jump to the rest of original code */
	orig[replaced_bytes] = 0xe9;
	u32_to_str(orig + replaced_bytes + 1, (uint32_t)(uintptr_t)addr + replaced_bytes - (uintptr_t)orig - replaced_bytes - 5);

	/* patch the original code to do a jump */
	buf[0] = 0xe9;
	u32_to_str(buf + 1, (uint32_t)(uintptr_t)fn - addr - 5);
	memset(buf + 5, 0x90, replaced_bytes - 5);
	patch_mem(addr, buf, replaced_bytes);

	*orig_fn = orig;
}

void
restore_mem(void)
{
	struct mem_region_4kb *reg_4k;
	struct mem_region_1mb *reg_1m;
	unsigned i, j, b;
	DWORD prevProt;
	void *addr;

	for (i = 0; i < 4096; i++) {
		reg_1m = g_mem_map[i];
		if (!reg_1m) {
			continue;
		}

		for (j = 0; j < 256; j++) {
			reg_4k = reg_1m->pages[j];
			if (!reg_4k) {
				continue;
			}

			addr = (void *)(uintptr_t)(i * 1024 * 1024 + j * 4096);
			VirtualProtect(addr, 4096, PAGE_EXECUTE_READWRITE, &prevProt);
			for (b = 0; b < 4096; b++) {
				if (reg_4k->byte_mask[b]) {
					*(char *)(addr + b) = *(char *)(reg_4k->data + b);
				}
			}
			VirtualProtect(addr, 4096, prevProt, NULL);
		}
	}
}

static void __attribute__((constructor))
common_static_init(void)
{
	memset(g_nops, 0x90, sizeof(g_nops));
}

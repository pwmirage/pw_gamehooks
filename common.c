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
#include <errno.h>
#include <windows.h>
#include <assert.h>
#include <math.h>
#include <io.h>
#include <keystone/keystone.h>

#include "pw_api.h"

int
split_string_to_words(char *input, char **argv, int *argc)
{
    char *c, *start;
    bool in_quotes = false;
	int cur_argc = 0;
    int rc = 0;

    #define NEW_WORD() \
    ({ \
        *c = 0; \
		if (*start) { \
			argv[cur_argc++] = start; \
			if (cur_argc == *argc) { \
				break; \
			} \
		} \
        start = c + 1; \
    })

    c = start = input;
    while (*c) {
        if (*c == '"') {
            if (in_quotes) {
                if (*(c + 1) != ' ' && *(c + 1) != 0) {
                    /* characters right after quote */
                    return -EINVAL;
                }

                in_quotes = false;
                NEW_WORD();
				if (*(c + 1) != 0) { /* unless it's the end of input */
					*(c + 1) = 0;  /* skip the space too */
					c++; start++;
				}
            } else {
                if (c > input && *(c - 1) != 0) {
                    /* quote right after characters (a space would be modified into NUL) */
                    return -EINVAL;
                }

                start = c + 1; /* dont include opening quote in the stirng */
                in_quotes = true;
            }
        } else if (*c == ' ' && !in_quotes) {
            NEW_WORD();
        }
        c++;
    }

    if (*c == 0) {
        do {
            NEW_WORD();
        } while (0);
    }

	#undef NEW_WORD

	*argc = cur_argc;
    return rc;
}

void *
ring_buffer_push(struct ring_buffer *ring, void *data)
{
	void **slot = &ring->entries[ring->last_idx++ % ring->size];
	void *prev = *slot;

	if ((ring->last_idx % ring->size) == (ring->first_idx % ring->size)) {
		/* we made a loop and will now override the "first" entry", so make
		 * the second first the new first */
		ring->first_idx++;
	}

	*slot = data;
	return prev;
}

size_t
ring_buffer_count(struct ring_buffer *ring)
{
	return (ring->last_idx - ring->first_idx) % ring->size;
}

void *
ring_buffer_pop(struct ring_buffer *ring)
{
	if (ring_buffer_count(ring) == 0) {
		return NULL;
	}

	return &ring->entries[ring->last_idx-- % ring->size];
}

void *
ring_buffer_peek(struct ring_buffer *ring, int off)
{
	/* no wrap-around checks */
	return ring->entries[(ring->first_idx + off) % ring->size];
}

struct ring_buffer *
ring_buffer_alloc(size_t size)
{
	struct ring_buffer *ring;

	ring = calloc(1, sizeof(*ring) + sizeof(*ring->entries) * size);
	ring->size = size;
	return ring;
}

struct ring_buffer_sp_sc {
	uint32_t head;
	uint32_t tail;
	uint32_t size;
	void *buf[0];
};

int
ring_buffer_sp_sc_push(struct ring_buffer_sp_sc *ring, void *val)
{
	uint32_t head, tail;
	uint32_t free_count;

	if (!malloc) {
		return -ENOMEM;
	}

	head = ring->head;
	__asm__("" ::: "memory"); /* rmb() - noop on x86 */
	tail = ring->tail;

	free_count = (ring->size + tail - head);
	if (free_count == 0) {
		return -ENOSPC;
	}

	ring->buf[head & (ring->size - 1)] = val;
	__asm__("" ::: "memory"); /* wmb() */
	ring->head = head + 1;
	return 0;
}

void *
ring_buffer_sp_sc_pop(struct ring_buffer_sp_sc *ring)
{
	uint32_t head, tail;
	uint32_t avail_count;
	void *ret;

	tail = ring->tail;
	__asm__("" ::: "memory"); /* rmb() - noop on x86 */
	head = ring->head;

	avail_count = tail - head;
	if (avail_count == 0) {
		return NULL;
	}

	ret = ring->buf[tail & (ring->size - 1)];

	__asm__("" ::: "memory"); /* mb() - make sure we read before we write,
								 although not needed on x86. */
	ring->tail = tail + 1;

	return ret;
}

int
ring_buffer_sp_sc_size(struct ring_buffer_sp_sc *ring)
{
	return ring->size;
}

struct ring_buffer_sp_sc *
ring_buffer_sp_sc_new(int count)
{
	struct ring_buffer_sp_sc *ret = calloc(1, sizeof(*ret) + sizeof(void *) * count);

	if (!ret) {
		return NULL;
	}

	ret->size = count;
	return ret;
}

struct mem_region_4kb {
	char data[4096];
	/* which bytes were overwritten */
	bool byte_mask[4096];
};

struct mem_region_1mb {
	struct mem_region_4kb *pages[256];
};

struct mem_region_1mb *g_mem_map[4096];

enum patch_mem_type {
	PATCH_MEM_T_RAW,
	PATCH_MEM_T_TRAMPOLINE,
	PATCH_MEM_T_TRAMPOLINE_FN
};

struct patch_mem_t {
	enum patch_mem_type type;
    uintptr_t addr;
    int replaced_bytes;
    char asm_code[0x1000];
	struct patch_mem_t *next;
};

static struct patch_mem_t *g_static_patches;

void
_patch_mem_unsafe(uintptr_t addr, const char *buf, unsigned num_bytes)
{
	DWORD prevProt, prevProt2;

	VirtualProtect((void *)addr, num_bytes, PAGE_EXECUTE_READWRITE, &prevProt);
	memcpy((void *)addr, buf, num_bytes);
	VirtualProtect((void *)addr, num_bytes, prevProt, &prevProt2);
}

void
_patch_mem_u32_unsafe(uintptr_t addr, uint32_t u32)
{
	union {
		char c[4];
		uint32_t u;
	} u;

	u.u = u32;
	_patch_mem_unsafe(addr, u.c, 4);
}

void
_patch_jmp32_unsafe(uintptr_t addr, uintptr_t fn)
{
	_patch_mem_u32_unsafe(addr + 1, fn - addr - 5);
}

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
	DWORD prevProt, prevProt2;

	backup_mem(addr, num_bytes);
	VirtualProtect((void *)addr, num_bytes, PAGE_EXECUTE_READWRITE, &prevProt);
	memcpy((void *)addr, buf, num_bytes);
	VirtualProtect((void *)addr, num_bytes, prevProt, &prevProt2);
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

void
trampoline_call(uintptr_t addr, unsigned replaced_bytes, void *fn)
{
	char buf[32];
	char *code;
	DWORD prevprot;

	assert(replaced_bytes >= 5 && replaced_bytes <= 64);
	code = VirtualAlloc(NULL, (14 + replaced_bytes + 0xFFF) & ~0xFFF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (code == NULL) {
		MessageBox(NULL, "malloc failed", "Status", MB_OK);
		return;
	}

	/* prepare the code to jump to */
	code[0] = 0x60; /* pushad */
	code[1] = 0x9c; /* pushfd */
	code[2] = 0xe8; /* call */
	u32_to_str(code + 3, (uintptr_t)fn - (uintptr_t)code - 2 - 5); /* fn rel addr */
	code[7] = 0x9d; /* popfd */
	code[8] = 0x61; /* popad */
	memcpy(code + 9, (void *)addr, replaced_bytes); /* replaced instructions */
	code[9 + replaced_bytes] = 0xe9; /* jmp */
	u32_to_str(code + 10 + replaced_bytes, /* jump back rel addr */
			addr + replaced_bytes - ((uintptr_t)code + 9 + replaced_bytes) - 5);

	VirtualProtect(code, 14 + replaced_bytes, PAGE_EXECUTE_READ, &prevprot);

	/* jump to new code */
	buf[0] = 0xe9;
	u32_to_str(buf + 1, (uintptr_t)code - addr - 5);
	memset(buf + 5, 0x90, replaced_bytes - 5);
	patch_mem(addr, buf, replaced_bytes);
}

void *
trampoline_buf(uintptr_t addr, unsigned replaced_bytes, const char *buf, unsigned num_bytes)
{
	char tmpbuf[32];
	char *code;
	DWORD prevprot;

	assert(replaced_bytes >= 5 && replaced_bytes <= 64);
	code = VirtualAlloc(NULL, (9 + num_bytes + replaced_bytes + 0xFFF) & ~0xFFF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (code == NULL) {
		MessageBox(NULL, "malloc failed", "Status", MB_OK);
		return NULL;
	}

	/* prepare the code to jump to */
	code[0] = 0x60; /* pushad */
	code[1] = 0x9c; /* pushfd */
	memcpy(code + 2, buf, num_bytes);
	code[2 + num_bytes] = 0x9d; /* popfd */
	code[3 + num_bytes] = 0x61; /* popad */
	memcpy(code + 4 + num_bytes, (void *)addr, replaced_bytes); /* replaced instructions */
	code[4 + num_bytes + replaced_bytes] = 0xe9; /* jmp */
	u32_to_str(code + 5 + num_bytes + replaced_bytes, /* jump back rel addr */
			addr + replaced_bytes - ((uintptr_t)code + 4 + num_bytes + replaced_bytes) - 5);

	VirtualProtect(code, 14 + replaced_bytes, PAGE_EXECUTE_READ, &prevprot);

	/* jump to new code */
	tmpbuf[0] = 0xe9;
	u32_to_str(tmpbuf + 1, (uintptr_t)code - addr - 5);
	memset(tmpbuf + 5, 0x90, replaced_bytes - 5);
	patch_mem(addr, tmpbuf, replaced_bytes);

	return code + 2;
}

void
trampoline_fn(void **orig_fn, unsigned replaced_bytes, void *fn)
{
	uint32_t addr = (uintptr_t)*orig_fn;
	char orig_code[32];
	char buf[32];
	char *orig;
	DWORD oldprot;

	memcpy(orig_code, (void *)addr, replaced_bytes);

	orig = VirtualAlloc(NULL, (replaced_bytes + 5 + 0xFFF) & ~0xFFF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (orig == NULL) {
		MessageBox(NULL, "malloc failed", "Status", MB_OK);
		return;
	}

	/* copy original code to a buffer */
	memcpy(orig, (void *)addr, replaced_bytes);
	/* follow it by a jump to the rest of original code */
	orig[replaced_bytes] = 0xe9;
	u32_to_str(orig + replaced_bytes + 1, (uint32_t)(uintptr_t)addr + replaced_bytes - (uintptr_t)orig - replaced_bytes - 5);

	VirtualProtect(orig, replaced_bytes + 5, PAGE_EXECUTE_READ, &oldprot);

	/* patch the original code to do a jump */
	buf[0] = 0xe9;
	u32_to_str(buf + 1, (uint32_t)(uintptr_t)fn - addr - 5);
	memset(buf + 5, 0x90, replaced_bytes - 5);
	patch_mem(addr, buf, replaced_bytes);

	*orig_fn = orig;
}

void
trampoline_winapi_fn(void **orig_fn, void *fn)
{
	uint32_t addr = (uintptr_t)*orig_fn;
	char buf[7];

	buf[0] = 0xe9; /* jmp */
	u32_to_str(buf + 1, (uint32_t)(uintptr_t)fn - addr);
	buf[5] = 0xeb; /* short jump */
	buf[6] = 0xf9; /* 7 bytes before */

	/* override 5 preceeding bytes (nulls) and 2 leading bytes */
	patch_mem(addr - 5, buf, 7);
	*orig_fn += 2;
}

static int
assemble_trampoline(uintptr_t addr, int replaced_bytes,
		char *asm_buf, unsigned char **out)
{
	unsigned char *code, *c;
	unsigned char *tmpcode;
	int len;
	DWORD prevprot;

	assert(replaced_bytes >= 5 && replaced_bytes <= 64);
	code = c = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (code == NULL) {
		return -ENOMEM;
	}

	char *asm_org = strstr(asm_buf, TRAMPOLINE_ORG);
	if (asm_org != NULL &&
			(*(asm_org + sizeof(TRAMPOLINE_ORG) - 1) == ';' ||
			 *(asm_org + sizeof(TRAMPOLINE_ORG) - 1) == 0)) {
		/* First assemble everything before the org, then copy org, and finally
		 * assemble the rest  */
		asm_org[0] = 0;
		len = assemble_x86((uintptr_t)c, asm_buf, &tmpcode);
		if (len < 0) {
			VirtualFree(code, 0x1000, MEM_RELEASE);
			return len;
		}

		if (len > 0) {
			memcpy(c, tmpcode, len);
			c += len;
		}

		memcpy(c, (void *)addr, replaced_bytes); /* replaced instructions */
		c += replaced_bytes;

		asm_buf = asm_org + strlen(TRAMPOLINE_ORG);
	}

	len = assemble_x86((uintptr_t)c, asm_buf, &tmpcode);
	if (len < 0) {
		VirtualFree(code, 0x1000, MEM_RELEASE);
		return len;
	}

	memcpy(c, tmpcode, len);
	c += len;

	char tmp[1024];
	size_t tmplen = 0;
	int i, l = c - code;

	*c++ = 0xe9; /* jmp */
	u32_to_str(c, /* jump back rel addr */
			addr + replaced_bytes - ((uintptr_t)c - 1) - 5);
	c += 4;

	VirtualProtect(code, 0x1000, PAGE_EXECUTE_READ, &prevprot);
	*out = code;
	return c - code;
}

void
trampoline_fn_static_add(void **orig_fn, int replaced_bytes, void *fn)
{
	struct patch_mem_t *t;
	va_list args;
	int rc;
	char *code;
	char *c;

	assert(replaced_bytes >= 5 && replaced_bytes <= 64);
	t = calloc(1, sizeof(*t));
	if (!t) {
		MessageBox(NULL, "malloc failed", "Status", MB_OK);
		assert(false);
		return;
	}

	t->type = PATCH_MEM_T_TRAMPOLINE_FN;
	t->addr = (uintptr_t)(void *)orig_fn;
	t->replaced_bytes = replaced_bytes;
	*(void **)t->asm_code = fn;

	t->next = g_static_patches;
	g_static_patches = t;
}

void
trampoline_static_add(uintptr_t addr, int replaced_bytes, const char *asm_fmt, ...)
{
	struct patch_mem_t *t;
	va_list args;
	int rc;
	char *code;
	char *c;

	assert(replaced_bytes >= 5 && replaced_bytes <= 64);
	t = calloc(1, sizeof(*t));
	if (!t) {
		MessageBox(NULL, "malloc failed", "Status", MB_OK);
		assert(false);
		return;
	}

	t->type = PATCH_MEM_T_TRAMPOLINE;
	t->addr = addr;
	t->replaced_bytes = replaced_bytes;

	va_start(args, asm_fmt);
	vsnprintf(t->asm_code, sizeof(t->asm_code), asm_fmt, args);
	va_end(args);

	c = t->asm_code;
	while (*c) {
		if (*c == '\t') {
			*c = ' ';
		}
		c++;
	}

	t->next = g_static_patches;
	g_static_patches = t;
}

void
patch_mem_static_add(uintptr_t addr, int replaced_bytes, const char *asm_fmt, ...)
{
	struct patch_mem_t *t;
	va_list args;
	int rc;
	char *code;
	char *c;

	t = calloc(1, sizeof(*t));
	if (!t) {
		MessageBox(NULL, "malloc failed", "Status", MB_OK);
		assert(false);
		return;
	}

	t->type = PATCH_MEM_T_RAW;
	t->addr = addr;
	t->replaced_bytes = replaced_bytes;

	va_start(args, asm_fmt);
	vsnprintf(t->asm_code, sizeof(t->asm_code), asm_fmt, args);
	va_end(args);

	c = t->asm_code;
	while (*c) {
		if (*c == '\t') {
			*c = ' ';
		}
		c++;
	}

	t->next = g_static_patches;
	g_static_patches = t;
}

static void
process_static_patch_mem(struct patch_mem_t *p)
{
	char tmp[0x1000];
	unsigned char *code;
	size_t tmplen = 0;
	int len = 0;

	switch(p->type) {
	case PATCH_MEM_T_RAW: {
		len = assemble_x86(p->addr, p->asm_code, &code);
		if (len < 0) {
			pw_log_color(0xFF0000, "patching %d bytes at 0x%x: can't assemble, invalid instruction", len, p->addr);
			return;
		}

		if (len > p->replaced_bytes) {
			pw_log_color(0xFF0000, "patching %d bytes at 0x%x: assembled code takes %d bytes and doesn't fit (max %d)", len, p->addr, len, p->replaced_bytes);
			return;
		}

		memcpy(tmp, code, len);
		if (len < p->replaced_bytes) {
			memset(tmp + len, 0x90, p->replaced_bytes - len);
		}
		patch_mem(p->addr, tmp, p->replaced_bytes);
		break;
	}
	case PATCH_MEM_T_TRAMPOLINE: {
		len = assemble_trampoline(p->addr, p->replaced_bytes, p->asm_code, &code);

		/* jump to new code */
		tmp[0] = 0xe9;
		u32_to_str(tmp + 1, (uintptr_t)code - p->addr - 5);
		memset(tmp + 5, 0x90, p->replaced_bytes - 5);
		patch_mem(p->addr, tmp, p->replaced_bytes);
		break;
	}
	case PATCH_MEM_T_TRAMPOLINE_FN: {
		trampoline_fn((void **)p->addr, p->replaced_bytes, *(void **)p->asm_code);
		break;
	}
	}
}

void
patch_mem_static_init(void)
{
	struct patch_mem_t *p = g_static_patches;

	while (p) {
		process_static_patch_mem(p);
		free(p);
		p = p->next;
	}
}

void
restore_mem(void)
{
	struct mem_region_4kb *reg_4k;
	struct mem_region_1mb *reg_1m;
	unsigned i, j, b;
	DWORD prevProt, prevProt2;
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
			VirtualProtect(addr, 4096, prevProt, &prevProt2);
		}
	}
}

static ks_engine *g_ks_engine;
static unsigned char *g_ks_buf;

int
assemble_x86(uint32_t addr, const char *in, unsigned char **out)
{
	ks_free(g_ks_buf);
	g_ks_buf = NULL;
	size_t size, icount;
	ks_err rc;

	rc = ks_asm(g_ks_engine, in, addr, &g_ks_buf, &size, &icount);
	if (rc != KS_ERR_OK)
	{
		return -ks_errno(g_ks_engine);
	}

	*out = g_ks_buf;
	return size;
}

void
common_static_init(void)
{
	ks_err err;

	memset(g_nops, 0x90, sizeof(g_nops));

	err = ks_open(KS_ARCH_X86, KS_MODE_32, &g_ks_engine);
	if (err != KS_ERR_OK)
	{
		MessageBox(NULL, "Failed to init ks engine", "Error", MB_OK);
		assert(false);
	}
}

void
common_static_fini(void)
{
    ks_free(g_ks_buf);
    ks_close(g_ks_engine);
}

/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021 Darek Stojaczyk for pwmirage.com
 */

#ifndef PW_GAMEHOOK_EXT_H
#define PW_GAMEHOOK_EXT_H

#include <stddef.h>

#ifdef DLLEXPORT
#define APICALL __declspec(dllexport)
#else
#define APICALL __declspec(dllimport)
#endif

typedef size_t (*crash_handler_cb)(char *buf, size_t bufsize, void *parent_hwnd, void *ctx);

APICALL void setup_crash_handler(crash_handler_cb cb, void *ctx);
APICALL void remove_crash_handler(void);
APICALL void handle_crash(void *winapi_exception_info);

struct mem_region {
    char name[28];
    uint32_t size;
    void *data;
};

APICALL struct mem_region *mem_region_get(const char *name, uint32_t size);
APICALL void **mem_region_get_u32(const char *name);
APICALL void mem_region_free(struct mem_region *mem);

#endif /* PW_GAMEHOOK_EXT_H */

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

#endif /* PW_GAMEHOOK_EXT_H */

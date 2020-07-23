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

#include <windows.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>

/* we want to use pointers in our game structs for convenience,
 * so make sure this lib is compiled as 32bit, just like the original
 * PW client
 */

/* real game structures
 * fields named "unk" are simply unknown and are used only
 * as a padding.
 */

struct player {
	char unk1[60];
	float pos_x;
	float pos_z;
	float pos_y;
	char unk2[1072];
	uint32_t hp;
	char unk3[1436];
	uint32_t target_id;
};

struct mob {
	char unk1[60];
	float pos_x;
	float pos_z;
	float pos_y;
	char unk2[108];
	uint32_t type; /* 6=mob, 7=npc */
	char unk3[100];
	uint32_t id;
	char unk4[12];
	uint32_t hp;
	char unk5[236];
	uint32_t disappear_count; /* number of ticks after mob death */
	uint32_t disappear_maxval; /* ticks before the mob disappears */
	char unk6[128];
	uint32_t state; /* walking, chasing, returning, or just-died */
};

struct mob_list {
	char unk1[20];
	uint32_t count;
	char unk2[56];
	struct mob_array {
		struct mob *mob[0];
	} *mobs;
};

struct world_objects {
	char unk1[32];
	char unk2[4]; /* player list? */
	struct mob_list *moblist;
};

struct game_data {
	char unk1[8];
	struct world_objects *wobj;
	char unk2[20];
	struct player *player;
	char unk3[108];
	uint32_t logged_in; /* 2 = logged in */
};

struct app_data {
	struct game_data *game;
};

extern HMODULE g_game;
extern HWND g_window;
extern struct app_data *g_pw_data;
extern void (*pw_select_target)(int id);
extern void (*pw_use_skill)(int skill_id, unsigned char pvp_mask, int num_targets, int *target_ids);

HWND pw_wait_for_win(void);
HMODULE pw_find_pwi_game_data(void);
void pw_spawn_debug_window(void);
void pw_static_init(void);

void patch_mem(uintptr_t addr, const char *buf, unsigned num_bytes);
void patch_mem_u32(uintptr_t addr, uint32_t u32);
void patch_mem_u16(uintptr_t addr, uint16_t u16);
void *trampoline(uintptr_t addr, unsigned replaced_bytes, const char *buf, unsigned num_bytes);
void u32_to_str(char *buf, uint32_t u32);

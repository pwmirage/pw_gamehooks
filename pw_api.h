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

struct object {
	char unk1[60];
	float pos_x;
	float pos_z;
	float pos_y;
	char unk2[108];
	uint32_t type; /* 3=player, 6=mob, 7=npc */
};

#define ISPLAYERID(id)  ((id) && !((id) & 0x80000000))
#define ISNPCID(id)	 (((id) & 0x80000000) && !((id) & 0x40000000))
#define ISMATTERID(id)  (((id) & 0xC0000000) == 0xC0000000)

struct skill {
	char unk1[4];
	void *unk2;
	int id;
	char unk3[12];
	bool on_cooldown;
};

struct player {
	struct object obj;
	char unk2[960];
	uint32_t hp;
	char unk3[1436];
	uint32_t target_id;
	char unk4[424];
	struct skill *cur_skill;
};

struct mob {
	struct object obj;
	char unk3[100];
	uint32_t id;
	char unk4[12];
	uint32_t hp;
	char unk5[236];
	uint32_t disappear_count; /* number of ticks after mob death */
	uint32_t disappear_maxval; /* ticks before the mob disappears */
	char unk6[128];
	uint32_t state; /* 4=walking, 5=attacking, 7=chasing, ?=returning, ?=just-died, 13=running away */
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
extern void (*pw_normal_attack)(unsigned char pvp_mask);

/*
 * alive_flag:
 *   1 target must be alive
 *   2 target must be dead
 *   0 don't care
 */
extern struct object * __thiscall (*pw_get_object)(struct world_objects *world, int id, int alive_filter);
/*
 * touch_type:
 *   1 melee
 *   2 skill
 *   3 talk
 *   0 other
 */
extern unsigned __thiscall (*pw_can_touch_target)(struct player *player, float tgt_coords[3], float tgt_radius, int touch_type, float max_melee_dist);
extern void __thiscall (*pw_on_touch)(void *unk1, unsigned unk2);

HWND pw_wait_for_win(void);
HMODULE pw_find_pwi_game_data(void);
void pw_spawn_debug_window(void);
void pw_static_init(void);

void patch_mem(uintptr_t addr, const char *buf, unsigned num_bytes);
void patch_mem_u32(uintptr_t addr, uint32_t u32);
void patch_mem_u16(uintptr_t addr, uint16_t u16);
void *trampoline(uintptr_t addr, unsigned replaced_bytes, const char *buf, unsigned num_bytes);
void trampoline_fn(void **orig_fn, unsigned replaced_bytes, void *fn);
void u32_to_str(char *buf, uint32_t u32);

bool is_settings_win_visible(void);
void show_settings_win(bool show);

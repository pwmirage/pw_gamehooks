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

#ifndef PW_API_H
#define PW_API_H

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
	char unk4b[36];
	uint32_t max_hp;
	char unk5[196];
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

struct ui_dialog {
	char unk1[84];
	uint32_t pos_x;
	uint32_t pos_y;
};

struct game_data {
	char unk1[4];
	struct ui_data *ui;
	struct world_objects *wobj;
	char unk2[20];
	struct player *player;
	char unk3[108];
	uint32_t logged_in; /* 2 = logged in */
};

struct ui_data {
	char unk1[8];
	void *ui_manager;
};

struct app_data {
	struct game_data *game;
};

extern HMODULE g_game;
extern HWND g_window;
extern struct app_data *g_pw_data;
extern void (*pw_move)(float *cur_pos, float *cur_pos_unused, int time, float speed, int move_mode, short timestamp);
extern void (*pw_stop_move)(float *dest_pos, float speed, int move_mode, unsigned dir, short timestamp, int time);
extern void (*pw_select_target)(int id);
extern void (*pw_use_skill)(int skill_id, unsigned char pvp_mask, int num_targets, int *target_ids);
extern void (*pw_normal_attack)(unsigned char pvp_mask);
extern unsigned __thiscall (*pw_load_configs)(struct game_data *game, void *unk1, int unk2);
extern void __thiscall (*pw_console_log)(void *ui_manager, const wchar_t *msg, unsigned argb_color);
extern unsigned char (*glb_CompressDirH)(float dirX, float dirZ);

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

static struct ui_dialog * __thiscall
(*pw_get_dialog)(void *ui_manager, const char *name) = (void *)0x6c94b0;

static void * __thiscall
(*pw_get_dialog_item)(struct ui_dialog *dialog, const char *name) = (void *)0x6d4550;

static int * __thiscall
(*pw_set_label_text)(void *label, const wchar_t *name) = (void *)0x6d7310;

HWND pw_wait_for_win(void);
HMODULE pw_find_pwi_game_data(void);

void pw_populate_console_log(void);
int pw_vlog_acolor(unsigned argb_color, const char *fmt, va_list args);
int pw_log_acolor(unsigned argb_color, const char *fmt, ...);
int pw_log_color(unsigned rgb_color, const char *fmt, ...);
int pw_log(const char *fmt, ...);

#endif /* PW_API_H */

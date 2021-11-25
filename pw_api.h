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
	char unk3[524];
	struct skill *cur_skill;
	char unk4[908];
	uint32_t target_id;
	char unk5[424];
	struct skill *prep_skill;
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
	char unk1[28];
	struct game_data *game;
	char unk2[1016];
	bool is_render_active;
};

extern HMODULE g_game;
extern HWND g_window;

static struct app_data *g_pw_data = (void *)0x927630;
static void (*pw_select_target)(int id) = (void *)0x5a8080;
static void (*pw_move)(float *cur_pos, float *cur_pos_unused, int time, float speed, int move_mode, short timestamp) = (void *)0x5a7f70;
static void (*pw_stop_move)(float *dest_pos, float speed, int move_mode, unsigned dir, short timestamp, int time) = (void *)0x5a8000;
static void (*pw_use_skill)(int skill_id, unsigned char pvp_mask, int num_targets, int *target_ids) = (void *)0x5a8a20;
static bool __thiscall (*pw_try_use_skill)(struct player *host_player, int skill_id, unsigned is_combo, unsigned target_id, int force_atk) = (void *)0x4559d0;
static int __thiscall (*pw_do_cast_skill)(struct player *host_player, int skill_id, int force_atk) = (void *)0x45a3a0;
static void * __thiscall (*pw_get_skill_by_id)(struct player *host_player, int id, bool unk) = (void *)(0x459e50);
static void (*pw_normal_attack)(unsigned char pvp_mask) = (void *)0x5a80c0;
static void __thiscall (*pw_console_log)(void *ui_manager, const wchar_t *msg, unsigned argb_color) = (void *)0x553cc0;
static void __thiscall (*pw_console_cmd)(void *ui_manager, const wchar_t *msg) = (void *)0x54b440;
static int __thiscall (*pw_read_local_cfg_opt)(void *unk1, const char *section, const char *name, int def_val) = (void *)0x6ff590;
static bool __thiscall (*pw_save_local_cfg_opt)(void *unk1, const char *section, const char *name, int val) = (void *)0x6ff810;
static unsigned __thiscall (*pw_load_configs)(struct game_data *game, void *unk1, int unk2) = (void *)0x431f30;
static unsigned char (*pw_xz_dir_to_byte)(float dirX, float dirZ) = (void *)0x4179a0;
static bool __thiscall (*pw_translate3dpos2screen)(void *viewport, float v3d[3], float v2d[3]) = (void *)0x71b1c0;
static void __thiscall (*pw_add_chat_message)(void *cecgamerun, const wchar_t *str, char channel, int player_id, const wchar_t *name, char unk, char emote) = (void *)0x552ea0;
static void (*pw_get_item_info)(unsigned char inv_id, unsigned char slot_id) = (void *)0x5a85b0;
static unsigned __thiscall (*pw_game_tick)(struct game_data *game, unsigned tick_time) = (void *)0x430bd0;
static int __cdecl (*pw_get_skill_execute_time)(int skill_id, int unk) = (void *)0x4c77a8;
static bool __thiscall (*pw_is_casting_skill)(void) = (void *)0x458ec0;

/*
 * alive_flag:
 *   1 target must be alive
 *   2 target must be dead
 *   0 don't care
 */
static struct object * __thiscall (*pw_get_object)(struct world_objects *world, int id, int alive_filter) = (void *)0x429510;

static unsigned __thiscall (*pw_can_do)(struct player *host_player, int do_what) = (void *)0x45af10;
/*
 * touch_type:
 *   1 melee
 *   2 skill
 *   3 talk
 *   0 other
 */
static unsigned __thiscall (*pw_can_touch_target)(struct player *player, float tgt_coords[3], float tgt_radius, int touch_type, float max_melee_dist) = (void *)0x458100;
static void __thiscall (*pw_on_touch)(void *unk1, unsigned unk2) = (void *)0x465140;

static struct ui_dialog * __thiscall (*pw_get_dialog)(void *ui_manager, const char *name) = (void *)0x6c94b0;
static void __thiscall * (*pw_dialog_show)(void *dialog, bool do_show, bool is_modal, bool is_active) = (void *)0x6d2e00;
static void * __thiscall (*pw_dialog_get_el)(struct ui_dialog *dialog, const char *name) = (void *)0x6d4550;
static bool  __thiscall (*pw_dialog_on_command)(struct ui_dialog *dialog, const char *command) = (void *)0x6d1c90;
static int * __thiscall (*pw_set_label_text)(void *label, const wchar_t *name) = (void *)0x6d7310;
static void __fastcall (*pw_item_add_ext_desc)(void *item) = (void *)0x48da10;
static int * __thiscall (*pw_item_desc_add_wstr)(void *item, wchar_t *wstr) = (void *)0x6f78d0;
static bool __thiscall (*pw_on_game_enter)(struct game_data *game, int world_id, float *player_pos) = (void *)0x42f950;
static void __fastcall (*pw_on_game_leave)(void) = (void *)0x42fbb0;

static void __thiscall (*pw_fashion_preview_set_item)(void *fashion_win, void *item, void *slot_el) = (void *)0x4c2d80;
static void * (*pw_alloc_item)(uint32_t id, uint32_t expire_time, uint32_t count, uint32_t id_space) = (void *)0x48cd70;
static void * (*pw_alloc)(size_t size) = (void *)0x6f5480;
static void (*pw_free)(void *addr) = (void *)0x6f5490;

void pw_populate_console_log(void);
int pw_vlog_acolor(unsigned argb_color, const char *fmt, va_list args);
int pw_log_acolor(unsigned argb_color, const char *fmt, ...);
int pw_log_color(unsigned rgb_color, const char *fmt, ...);
int pw_log(const char *fmt, ...);

#endif /* PW_API_H */

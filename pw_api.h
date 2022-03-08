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

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>
#include <tchar.h>
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
	char unk5[26];
	uint8_t is_in_cosmetician;
	char unk7[397];
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

struct ui_dialog_el {
	void **fn_tbl;
	char unk1[56];
	int type; /* 4 == text box */
};

struct ui_dialog {
	char unk[40];
	char *name;
	char unk1[40];
	uint32_t pos_x;
	uint32_t pos_y;
	char unk2[52];
	struct ui_dialog_el *focused_el;
};

struct game_data {
	void **fn_tbl;
	struct ui_data *ui;
	struct world_objects *wobj;
	char unk2[20];
	struct player *player;
	char unk3[108];
	uint32_t logged_in; /* 2 = logged in */
};

struct ui_manager {
	char unk1[20];
	struct ui_dialog *focused_dialog;
	char unk1b[500];
	void *map_manager;
	char unk2[104];
	struct ui_dialog *pet_quickbar_dialog;
	char unk3[132];
	void *dlg_manager;
	void *dlg_manager2;
	void *dlg_manager3;
	char unk4[524];
	bool show_ui;
};

struct ui_data {
	char unk1[8];
	struct ui_manager *ui_manager;
};

struct app_data {
	char unk1[24];
	void **config_man;
	struct game_data *game;
	void *unk2;
	void *unk3;
	void *gfx_man;
	char unk4[1004];
	bool is_render_active;
};

extern HMODULE g_game;
extern HWND g_window;

#ifdef PW_API_DEFINE
#define PW_CALL
#define PW_ADDR(addr_p) = (void *)addr_p
#else
#define PW_CALL extern
#define PW_ADDR(addr_p) ;
#endif


PW_CALL struct app_data *g_pw_data PW_ADDR(0x927630);
PW_CALL bool __thiscall (*pw_on_keydown)(void *ui_manager, int event, int keycode, unsigned mods) PW_ADDR(0x54f950);
PW_CALL void __thiscall (*pw_world_map_dlg_resize)(void *map, int *size) PW_ADDR(0x50b950);
PW_CALL void (*pw_select_target)(int id) PW_ADDR(0x5a8080);
PW_CALL void (*pw_move)(float *cur_pos, float *cur_pos_unused, int time, float speed, int move_mode, short timestamp) PW_ADDR(0x5a7f70);
PW_CALL void (*pw_stop_move)(float *dest_pos, float speed, int move_mode, unsigned dir, short timestamp, int time) PW_ADDR(0x5a8000);
PW_CALL void (*pw_use_skill)(int skill_id, unsigned char pvp_mask, int num_targets, int *target_ids) PW_ADDR(0x5a8a20);
PW_CALL bool __thiscall (*pw_try_use_skill)(struct player *host_player, int skill_id, unsigned is_combo, unsigned target_id, int force_atk) PW_ADDR(0x4559d0);
PW_CALL int __thiscall (*pw_do_cast_skill)(struct player *host_player, int skill_id, int force_atk) PW_ADDR(0x45a3a0);
PW_CALL void * __thiscall (*pw_get_skill_by_id)(struct player *host_player, int id, bool unk) PW_ADDR((0x459e50));
PW_CALL void (*pw_normal_attack)(unsigned char pvp_mask) PW_ADDR(0x5a80c0);
PW_CALL void __thiscall (*pw_console_log)(void *ui_manager, const wchar_t *msg, unsigned argb_color) PW_ADDR(0x553cc0);
PW_CALL void __cdecl (*pw_console_parse_cmd)(const char *cmd) PW_ADDR(0x54b570);
PW_CALL void __thiscall (*pw_console_exec_cmd)(void *ui_manager) PW_ADDR(0x54b740);
PW_CALL void __thiscall (*pw_console_cmd)(void *ui_manager, const wchar_t *msg) PW_ADDR(0x54b440);
PW_CALL int __thiscall (*pw_read_local_cfg_opt)(void *unk1, const char *section, const char *name, int def_val) PW_ADDR(0x6ff590);
PW_CALL bool __thiscall (*pw_save_local_cfg_opt)(void *unk1, const char *section, const char *name, int val) PW_ADDR(0x6ff810);
PW_CALL unsigned __thiscall (*pw_load_configs)(struct game_data *game, void *unk1, int unk2) PW_ADDR(0x431f30);
PW_CALL unsigned char (*pw_xz_dir_to_byte)(float dirX, float dirZ) PW_ADDR(0x4179a0);
PW_CALL bool __thiscall (*pw_translate3dpos2screen)(void *viewport, float v3d[3], float v2d[3]) PW_ADDR(0x71b1c0);
PW_CALL void __thiscall (*pw_add_chat_message)(void *cecgamerun, const wchar_t *str, char channel, int player_id, const wchar_t *name, char unk, char emote) PW_ADDR(0x552ea0);
PW_CALL void (*pw_get_item_info)(unsigned char inv_id, unsigned char slot_id) PW_ADDR(0x5a85b0);
PW_CALL unsigned __thiscall (*pw_game_tick)(struct game_data *game, unsigned tick_time) PW_ADDR(0x430bd0);
PW_CALL int __cdecl (*pw_get_skill_execute_time)(int skill_id, int unk) PW_ADDR(0x4c77a8);
PW_CALL bool __thiscall (*pw_is_casting_skill)(void) PW_ADDR(0x458ec0);

/*
 * alive_flag:
 *   1 target must be alive
 *   2 target must be dead
 *   0 don't care
 */
PW_CALL struct object * __thiscall (*pw_get_object)(struct world_objects *world, int id, int alive_filter) PW_ADDR(0x429510);

PW_CALL unsigned __thiscall (*pw_can_do)(struct player *host_player, int do_what) PW_ADDR(0x45af10);
/*
 * touch_type:
 *   1 melee
 *   2 skill
 *   3 talk
 *   0 other
 */
PW_CALL unsigned __thiscall (*pw_can_touch_target)(struct player *player, float tgt_coords[3], float tgt_radius, int touch_type, float max_melee_dist) PW_ADDR(0x458100);
PW_CALL void __thiscall (*pw_on_touch)(void *unk1, unsigned unk2) PW_ADDR(0x465140);

PW_CALL struct ui_dialog * __thiscall (*pw_get_dialog)(void *ui_manager, const char *name) PW_ADDR(0x6c94b0);
PW_CALL void __thiscall * (*pw_dialog_show)(struct ui_dialog *dialog, bool do_show, bool is_modal, bool is_active) PW_ADDR(0x6d2e00);
PW_CALL struct ui_dialog_el * __thiscall (*pw_dialog_get_el)(struct ui_dialog *dialog, const char *name) PW_ADDR(0x6d4550);
PW_CALL void __thiscall (*pw_dialog_change_focus)(struct ui_dialog *dialog, void *el) PW_ADDR(0x6d44f0);
PW_CALL void __thiscall (*pw_dialog_el_set_pos)(void *el, int x, int y) PW_ADDR(0x6d6b60);
PW_CALL void __thiscall (*pw_dialog_el_set_size)(void *el, int w, int h) PW_ADDR(0x6d6e40);
PW_CALL void __thiscall (*pw_dialog_el_set_accept_mouse_message)(void *el, bool accept) PW_ADDR(0x6d7430);
PW_CALL void __thiscall (*pw_bring_dialog_to_front)(void *ui_manager, struct ui_dialog *dialog) PW_ADDR(0x6c9180);
PW_CALL bool  __thiscall (*pw_dialog_on_command)(struct ui_dialog *dialog, const char *command) PW_ADDR(0x6d1c90);
PW_CALL void __thiscall (*pw_dialog_set_can_move)(struct ui_dialog *dialog, bool can_move) PW_ADDR(0x6d5460);
PW_CALL bool  __thiscall (*pw_dialog_is_shown)(struct ui_dialog *dialog) PW_ADDR(0x6d2fa0);
PW_CALL int * __thiscall (*pw_set_label_text)(void *label, const wchar_t *name) PW_ADDR(0x6d7310);
PW_CALL void __fastcall (*pw_item_add_ext_desc)(void *item) PW_ADDR(0x48da10);
PW_CALL int * __thiscall (*pw_item_desc_add_wstr)(void *item, wchar_t *wstr) PW_ADDR(0x6f78d0);
PW_CALL bool __thiscall (*pw_on_game_enter)(struct game_data *game, int world_id, float *player_pos) PW_ADDR(0x42f950);
PW_CALL void __fastcall (*pw_on_game_leave)(void) PW_ADDR(0x42fbb0);

PW_CALL void __thiscall (*pw_fashion_preview_set_item)(void *fashion_win, void *item, void *slot_el) PW_ADDR(0x4c2d80);

PW_CALL void __thiscall (*pw_open_character_window)(void *dlg_man, const char *win_name) PW_ADDR(0x501180);
PW_CALL void __thiscall (*pw_open_inventory_window)(void *dlg_man, const char *win_name) PW_ADDR(0x501120);
PW_CALL void __thiscall (*pw_open_skill_window)(void *dlg_man, const char *win_name) PW_ADDR(0x501200);
PW_CALL void __thiscall (*pw_open_action_window)(void *dlg_man2, const char *win_name) PW_ADDR(0x501830);
PW_CALL void __thiscall (*pw_open_team_window)(void *dlg_man2, const char *win_name) PW_ADDR(0x501880);
PW_CALL void __thiscall (*pw_open_friend_window)(void *dlg_man2, const char *win_name) PW_ADDR(0x5018e0);
PW_CALL void __thiscall (*pw_open_pet_window)(void *dlg_man2, const char *win_name) PW_ADDR(0x501800);
PW_CALL void __thiscall (*pw_pet_quickbar_command_attack)(struct ui_dialog *quickbar, void *unk) PW_ADDR(0x4f69c0);
PW_CALL void __thiscall (*pw_pet_quickbar_command_skill)(struct ui_dialog *quickbar, int unk1, int unk2, struct ui_dialog_el *skill_el) PW_ADDR(0x4f6b30);
PW_CALL void __thiscall (*pw_open_shop_window)(void *dlg_man, const char *win_name) PW_ADDR(0x5015c0);
PW_CALL void __thiscall (*pw_open_map_window)(void *map_man, const char *win_name) PW_ADDR(0x4e0c10);
//PW_CALL void __thiscall (*pw_open__window)(void *dlg_man, const char *win_name) PW_ADDR(0x);
PW_CALL void __thiscall (*pw_open_quest_window)(void *dlg_man, const char *win_name) PW_ADDR(0x501260);
PW_CALL void __thiscall (*pw_open_faction_window)(void *dlg_man2, const char *win_name) PW_ADDR(0x501910);
PW_CALL void __thiscall (*pw_open_help_window)(void *dlg_man3, const char *win_name) PW_ADDR(0x501c50);
PW_CALL void __thiscall (*pw_queue_action_raw)(void *unk, int action_id, int param0, int param1, int param2, int param3, int param4, int param5) PW_ADDR(0x433c40);

void pw_queue_action(int action_id, int param0, int param1, int param2, int param3, int param4, int param5);

/**
 * @param player host player
 * @param row 1-3 or 4-6,
 * 	1 = bottom quickbar row, 3 = top quickbar row
 * 	4 = bottom alt quickbar row, 6 = top alt quickbar row
 * @param col 0-9
 */
void pw_quickbar_command_skill(int row, int col);

PW_CALL void * (*pw_alloc_item)(uint32_t id, uint32_t expire_time, uint32_t count, uint32_t id_space) PW_ADDR(0x48cd70);
PW_CALL void * (*pw_alloc)(size_t size) PW_ADDR(0x6f5480);
PW_CALL void (*pw_free)(void *addr) PW_ADDR(0x6f5490);

typedef void (*mg_callback)(void *arg1, void *arg2);
int pw_game_thr_post_msg(mg_callback fn, void *arg1, void *arg2);

/* TODO move this to some internal header */
#define MG_CB_MSG (WM_USER + 165)

struct thread_msg_ctx {
	mg_callback cb;
	void *arg1;
	void *arg2;
};

void pw_ui_thread_sendmsg(mg_callback cb, void *arg1, void *arg2);
void pw_ui_thread_postmsg(mg_callback cb, void *arg1, void *arg2);

void pw_vlog_acolor(unsigned argb_color, const char *fmt, va_list args);
void pw_log_acolor(unsigned argb_color, const char *fmt, ...);
void pw_log_color(unsigned rgb_color, const char *fmt, ...);
void pw_log(const char *fmt, ...);
void pw_debuglog(int severity, const char *fmt, ...);
int parse_console_cmd(const char *in, char *out, size_t outlen);

#ifdef __cplusplus
}
#endif

#endif /* PW_API_H */

/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

#include <stdio.h>
#include <windows.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <wchar.h>
#include <inttypes.h>
#include <stdint.h>
#include <math.h>
#include <stdbool.h>
#include <assert.h>
#include <wingdi.h>

#include "pw_api.h"
#include "common.h"
#include "d3d.h"

enum hotkey_action {
	HOTKEY_A_NONE = 0,
	HOTKEY_A_SKILL_1_1,
	HOTKEY_A_SKILL_1_2,
	HOTKEY_A_SKILL_1_3,
	HOTKEY_A_SKILL_1_4,
	HOTKEY_A_SKILL_1_5,
	HOTKEY_A_SKILL_1_6,
	HOTKEY_A_SKILL_1_7,
	HOTKEY_A_SKILL_1_8,
	HOTKEY_A_SKILL_1_9,
	HOTKEY_A_SKILL_2_1,
	HOTKEY_A_SKILL_2_2,
	HOTKEY_A_SKILL_2_3,
	HOTKEY_A_SKILL_2_4,
	HOTKEY_A_SKILL_2_5,
	HOTKEY_A_SKILL_2_6,
	HOTKEY_A_SKILL_2_7,
	HOTKEY_A_SKILL_2_8,
	HOTKEY_A_SKILL_2_9,
	HOTKEY_A_SKILL_3_1,
	HOTKEY_A_SKILL_3_2,
	HOTKEY_A_SKILL_3_3,
	HOTKEY_A_SKILL_3_4,
	HOTKEY_A_SKILL_3_5,
	HOTKEY_A_SKILL_3_6,
	HOTKEY_A_SKILL_3_7,
	HOTKEY_A_SKILL_3_8,
	HOTKEY_A_SKILL_3_9,
	HOTKEY_A_SKILLF_1_1,
	HOTKEY_A_SKILLF_1_2,
	HOTKEY_A_SKILLF_1_3,
	HOTKEY_A_SKILLF_1_4,
	HOTKEY_A_SKILLF_1_5,
	HOTKEY_A_SKILLF_1_6,
	HOTKEY_A_SKILLF_1_7,
	HOTKEY_A_SKILLF_1_8,
	HOTKEY_A_SKILLF_2_1,
	HOTKEY_A_SKILLF_2_2,
	HOTKEY_A_SKILLF_2_3,
	HOTKEY_A_SKILLF_2_4,
	HOTKEY_A_SKILLF_2_5,
	HOTKEY_A_SKILLF_2_6,
	HOTKEY_A_SKILLF_2_7,
	HOTKEY_A_SKILLF_2_8,
	HOTKEY_A_SKILLF_3_1,
	HOTKEY_A_SKILLF_3_2,
	HOTKEY_A_SKILLF_3_3,
	HOTKEY_A_SKILLF_3_4,
	HOTKEY_A_SKILLF_3_5,
	HOTKEY_A_SKILLF_3_6,
	HOTKEY_A_SKILLF_3_7,
	HOTKEY_A_SKILLF_3_8,
	HOTKEY_A_PETSKILL_1, /* normal attack */
	HOTKEY_A_PETSKILL_2,
	HOTKEY_A_PETSKILL_3,
	HOTKEY_A_PETSKILL_4,
	HOTKEY_A_PETSKILL_5,
	HOTKEY_A_PETSKILL_6,
	HOTKEY_A_PETSKILL_7,
	HOTKEY_A_PETSKILL_8,
	HOTKEY_A_PETSKILL_9,
	HOTKEY_A_PET_MODE_FOLLOW,
	HOTKEY_A_PET_MODE_STOP,
	HOTKEY_A_HIDEUI,
	HOTKEY_A_CONSOLE,
	HOTKEY_A_ROLL,
	HOTKEY_A_MAP,
	HOTKEY_A_CHARACTER,
	HOTKEY_A_INVENTORY,
	HOTKEY_A_SKILLS,
	HOTKEY_A_QUESTS,
	HOTKEY_A_ACTIONS,
	HOTKEY_A_PARTY,
	HOTKEY_A_FRIENDS,
	HOTKEY_A_FACTION,
	HOTKEY_A_PETS,
	HOTKEY_A_HELP,
	HOTKEY_A_GM_CONSOLE,
};

union hotkey_mod_mask {
	struct {
		uint8_t shift : 1;
		uint8_t ctrl : 1;
		uint8_t alt : 1;
		uint8_t reserved : 5;
	};
	uint8_t combined;
};

struct hotkey {
	uint8_t key;
	union hotkey_mod_mask mods;
	uint8_t action;
	struct hotkey *next;
};

#define MAX_HOTKEYS 256
static struct hotkey *g_hotkeys[MAX_HOTKEYS];

static struct ui_dialog *
get_open_world_map_dialog(struct ui_manager *ui_man)
{
	struct ui_dialog *dialog;

	dialog = pw_get_dialog(ui_man, "GuildMap");
	if (pw_dialog_is_shown(dialog)) {
		return dialog;
	}

	dialog = pw_get_dialog(ui_man, "WorldMap");
	if (pw_dialog_is_shown(dialog)) {
		return dialog;
	}

	dialog = pw_get_dialog(ui_man, "WorldMapDetail");
	if (pw_dialog_is_shown(dialog)) {
		return dialog;
	}

	return NULL;
}

static float
dist_obj(struct object *obj1, struct object *obj2)
{
	float dist_tmp, dist;

	dist_tmp = obj1->pos_x - obj2->pos_x;
	dist_tmp *= dist_tmp;
	dist = dist_tmp;

	dist_tmp = obj1->pos_z - obj2->pos_z;
	dist_tmp *= dist_tmp;
	dist += dist_tmp;

	dist_tmp = obj1->pos_y - obj2->pos_y;
	dist_tmp *= dist_tmp;
	dist += dist_tmp;

	return sqrt(dist);
}

static void
select_closest_mob(void)
{
	struct game_data *game = g_pw_data->game;
	struct player *player = game->player;
	uint32_t mobcount = game->wobj->moblist->count;
	float min_dist = 999;
	uint32_t new_target_id = player->target_id;
	struct mob *mob;

	for (int i = 0; i < mobcount; i++) {
		float dist;

		mob = game->wobj->moblist->mobs->mob[i];
		if (mob->obj.type != 6) {
			/* not a monster */
			continue;
		}

		if (mob->disappear_count > 0) {
			/* mob is dead and already disappearing */
			continue;
		}

		dist = dist_obj(&mob->obj, &player->obj);
		if (dist < min_dist) {
			min_dist = dist;
			new_target_id = mob->id;
		}
	}

	pw_select_target(new_target_id);
}

extern bool g_disable_all_overlay;

enum hotkey_action
get_mapped_action(int event, int keycode)
{
	struct hotkey *hotkey;
	union hotkey_mod_mask mask = {};

	if (event == WM_SYSKEYDOWN) {
		mask.alt = 1;
	} else if (event != WM_KEYDOWN) {
		/* not important */
		return HOTKEY_A_NONE;
	}

	if (keycode >= MAX_HOTKEYS) {
		return HOTKEY_A_NONE;
	}

	if (keycode >= 'A' && keycode <= 'Z') {
		keycode += 'a' - 'A';
	}

	mask.shift = !!(GetAsyncKeyState(VK_SHIFT) & 0x8000);
	mask.ctrl = !!(GetAsyncKeyState(VK_CONTROL) & 0x8000);

	hotkey = g_hotkeys[keycode];
	while (hotkey) {
		if (hotkey->mods.combined == mask.combined) {
			break;
		}
		hotkey = hotkey->next;
	}

	if (!hotkey) {
		return HOTKEY_A_NONE;
	}

	return hotkey->action;
}

static bool __thiscall
hooked_pw_on_keydown(struct ui_manager *ui_man, int event, int keycode, unsigned mods)
{
	bool is_repeat = mods & 0x40000000;
	struct ui_dialog *dialog;
	enum hotkey_action action;

	if (g_pw_data->game->player->is_in_cosmetician) {
		return false;
	}

	pw_log("event=%x, keycode=%x, mods=%x", event, keycode, mods);

	action = get_mapped_action(event, keycode);
	switch (action) {
	case HOTKEY_A_GM_CONSOLE: {
		struct ui_dialog *dialog = pw_get_dialog(ui_man, "Win_GMConsole");
		if (pw_dialog_is_shown(dialog)) {
			pw_dialog_show(dialog, false, false, false);
		} else {
			pw_dialog_show(dialog, true, false, true);
		}
		return true;
	}
	default:
		break;
	}

	return false;

	switch(event) {
	case WM_CHAR:
		switch (keycode) {
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				pw_quickbar_command_skill(1, keycode - '1');
				return true;
			default:
				break;
		}
		if (!is_repeat) {
			switch (keycode) {
				case '~':
					d3d_console_toggle();
					return true;
				case 'x': {
					pw_queue_action(0x125, 0, 0, 0, 0, 0, 0);
					return true;
				}
				case 'm': {
					dialog = get_open_world_map_dialog(ui_man);
					if (dialog) {
						pw_dialog_on_command(dialog, "IDCANCEL");
					} else {
						pw_open_map_window(ui_man->map_manager, "bigmap");
					}
					return true;
				}
				case 'c':
					pw_open_character_window(ui_man->dlg_manager, "wcharacter");
					return true;
				case 'b':
					pw_open_inventory_window(ui_man->dlg_manager, "winventory");
					return true;
				case 'r':
					pw_open_skill_window(ui_man->dlg_manager, "wskill");
					return true;
				case 'q':
					pw_open_quest_window(ui_man->dlg_manager, "quest");
					return true;
				case 'e':
					pw_open_action_window(ui_man->dlg_manager2, "waction");
					return true;
				case 't':
					pw_open_team_window(ui_man->dlg_manager2, "wteam");
					return true;
				case 'f':
					pw_open_friend_window(ui_man->dlg_manager2, "wfriend");
					return true;
				case 'g':
					pw_open_faction_window(ui_man->dlg_manager2, "wfaction");
					return true;
				case 'p':
					pw_open_pet_window(ui_man->dlg_manager2, "wpet");
					return true;
				case 'l':
					pw_open_help_window(ui_man->dlg_manager3, "whelp");
					return true;
				default:
					break;
				    }
		}
		break;
	case WM_SYSCHAR:
		switch (keycode) {
			case '1': {
				struct ui_dialog *qbar = g_pw_data->game->ui->ui_manager->pet_quickbar_dialog;
				if (!pw_dialog_is_shown(qbar)) {
					return true;
				}

				pw_pet_quickbar_command_attack(qbar, (void *)0x926d38);
			}
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9': {
				struct ui_dialog *qbar;
				struct ui_dialog_el *el;
				char buf[16];

				qbar = g_pw_data->game->ui->ui_manager->pet_quickbar_dialog;
				if (!pw_dialog_is_shown(qbar)) {
					return true;
				}

				snprintf(buf, sizeof(buf), "Skill_%d", keycode - '0'); /* 1+ */
				el = pw_dialog_get_el(qbar, buf);
				pw_pet_quickbar_command_skill(qbar, 0, 0, el);
				return true;
			}
			case 'f': {
				struct ui_dialog *qbar = g_pw_data->game->ui->ui_manager->pet_quickbar_dialog;
				if (!pw_dialog_is_shown(qbar)) {
					return true;
				}

				pw_dialog_on_command(qbar, "follow");
				return true;
			}
			case 's': {
				struct ui_dialog *qbar = g_pw_data->game->ui->ui_manager->pet_quickbar_dialog;
				if (!pw_dialog_is_shown(qbar)) {
					return true;
				}

				pw_dialog_on_command(qbar, "stop");
				return true;
			}
			case 'h': {
				if (!is_repeat) {
					ui_man->show_ui = !ui_man->show_ui;
					g_disable_all_overlay = !ui_man->show_ui;
				}
			}
			default:
				break;
		}

	case WM_KEYDOWN:
		switch (keycode) {
			case VK_RETURN: {
				struct ui_dialog *dialog;
				struct ui_dialog_el *el;

				if (is_repeat) {
					break;
				}

				if (ui_man->focused_dialog && ui_man->focused_dialog->focused_el &&
						ui_man->focused_dialog->focused_el->type == 4) {
					/* text field focused */
					break;
				}

				dialog = pw_get_dialog(ui_man, "Win_Chat");
				el = pw_dialog_get_el(dialog, "DEFAULT_Txt_Speech");

				pw_bring_dialog_to_front(ui_man, dialog);
				((void __thiscall (*)(void *, bool)) el->fn_tbl[14])(el, 1);
				pw_dialog_change_focus(dialog, el);

				return true;
			}
			case VK_F1:
			case VK_F2:
			case VK_F3:
			case VK_F4:
			case VK_F5:
			case VK_F6:
			case VK_F7:
			case VK_F8:
				pw_quickbar_command_skill(4, keycode - VK_F1);
				return true;
			case VK_F9: {
				struct ui_dialog *dialog;

				if (is_repeat) {
					break;
				}

				pw_queue_action(220, 0, 0, 0, 0, 0, 0);
				dialog = pw_get_dialog(ui_man, "Win_Camera");
				if (!pw_dialog_is_shown(dialog)) {
					pw_dialog_show(dialog, true, false, true);
				} else {
					pw_dialog_show(dialog, false, false, false);
				}

				return true;
			}
			case 'g':
			case 'G':
			default:
				break;
		}
		break;
	default:
		break;
	}

	if (get_open_world_map_dialog(ui_man)) {
		return pw_on_keydown(ui_man, event, keycode, mods);
	}

	switch (event) {
	case WM_KEYDOWN:
		switch (keycode) {
			case VK_TAB:
				if (!is_repeat) {
					select_closest_mob();
				}
				return true;
			default:
				break;
		}
		break;
	default:
		break;
	}

	bool rc = pw_on_keydown(ui_man, event, keycode, mods);
	return rc;
}

TRAMPOLINE_FN(&pw_on_keydown, 7, hooked_pw_on_keydown);

/* disable hardcoded camera mode on F9 */
PATCH_MEM(0x4021c4, 5, "jmp 0x4021dc");

/* disable hardcoded character rolling on X keypress */
PATCH_MEM(0x40227e, 5, "jmp 0x402296");

/* no movement restrictions for camera mode */
//PATCH_MEM(0x405c1e, 2, "nop; nop");
/* increase camera movement boundaries (from 11) */
PATCH_MEM(0x85fc10, 4, ".float 30");

static void
_set_key(struct hotkey hotkey_p)
{
	struct hotkey *h = calloc(1, sizeof(*h));
	assert(h);

	memcpy(h, &hotkey_p, sizeof(*h));
	h->next = g_hotkeys[h->key];
	g_hotkeys[h->key] = h;
}

static void __attribute__((constructor))
init(void)
{
	_set_key((struct hotkey){ .key = 'g', .action = HOTKEY_A_GM_CONSOLE, .mods = { .ctrl = 1 }});
}
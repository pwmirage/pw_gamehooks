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

static bool __thiscall
hooked_pw_on_keydown(struct ui_manager *ui_man, int event, int keycode, unsigned mods)
{
	bool is_repeat = mods & 0x40000000;
	struct ui_dialog *dialog;

	if (g_pw_data->game->player->is_in_cosmetician) {
		return false;
	}

	//pw_log("event=%x, keycode=%x, mods=%x", event, keycode, mods);
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
				if ((GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
					struct ui_dialog *dialog = pw_get_dialog(ui_man, "Win_GMConsole");
					if (pw_dialog_is_shown(dialog)) {
						pw_dialog_show(dialog, false, false, false);
					} else {
						pw_dialog_show(dialog, true, false, true);
					}
				}
				return true;
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
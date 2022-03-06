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

#include "input.h"

#include "pw_api.h"
#include "common.h"
#include "d3d.h"
#include "csh.h"

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
	int key;
	union hotkey_mod_mask mods;
	uint8_t action;
	struct hotkey *next;
};

#define MAX_HOTKEYS 256
static struct hotkey *g_hotkeys[MAX_HOTKEYS];

const char *
mg_input_action_to_str(int id)
{
	switch (id) {
	case HOTKEY_A_SKILL_1_1: return "Skill 1 (Bottom bar)";
	case HOTKEY_A_SKILL_1_2: return "Skill 2 (Bottom bar)";
	case HOTKEY_A_SKILL_1_3: return "Skill 3 (Bottom bar)";
	case HOTKEY_A_SKILL_1_4: return "Skill 4 (Bottom bar)";
	case HOTKEY_A_SKILL_1_5: return "Skill 5 (Bottom bar)";
	case HOTKEY_A_SKILL_1_6: return "Skill 6 (Bottom bar)";
	case HOTKEY_A_SKILL_1_7: return "Skill 7 (Bottom bar)";
	case HOTKEY_A_SKILL_1_8: return "Skill 8 (Bottom bar)";
	case HOTKEY_A_SKILL_1_9: return "Skill 9 (Bottom bar)";
	case HOTKEY_A_SKILL_2_1: return "Skill 1 (Middle bar)";
	case HOTKEY_A_SKILL_2_2: return "Skill 2 (Middle bar)";
	case HOTKEY_A_SKILL_2_3: return "Skill 3 (Middle bar)";
	case HOTKEY_A_SKILL_2_4: return "Skill 4 (Middle bar)";
	case HOTKEY_A_SKILL_2_5: return "Skill 5 (Middle bar)";
	case HOTKEY_A_SKILL_2_6: return "Skill 6 (Middle bar)";
	case HOTKEY_A_SKILL_2_7: return "Skill 7 (Middle bar)";
	case HOTKEY_A_SKILL_2_8: return "Skill 8 (Middle bar)";
	case HOTKEY_A_SKILL_2_9: return "Skill 9 (Middle bar)";
	case HOTKEY_A_SKILL_3_1: return "Skill 1 (Top bar)";
	case HOTKEY_A_SKILL_3_2: return "Skill 2 (Top bar)";
	case HOTKEY_A_SKILL_3_3: return "Skill 3 (Top bar)";
	case HOTKEY_A_SKILL_3_4: return "Skill 4 (Top bar)";
	case HOTKEY_A_SKILL_3_5: return "Skill 5 (Top bar)";
	case HOTKEY_A_SKILL_3_6: return "Skill 6 (Top bar)";
	case HOTKEY_A_SKILL_3_7: return "Skill 7 (Top bar)";
	case HOTKEY_A_SKILL_3_8: return "Skill 8 (Top bar)";
	case HOTKEY_A_SKILL_3_9: return "Skill 9 (Top bar)";
	case HOTKEY_A_SKILLF_1_1: return "Alt. Skill 1 (Bottom bar)";
	case HOTKEY_A_SKILLF_1_2: return "Alt. Skill 2 (Bottom bar)";
	case HOTKEY_A_SKILLF_1_3: return "Alt. Skill 3 (Bottom bar)";
	case HOTKEY_A_SKILLF_1_4: return "Alt. Skill 4 (Bottom bar)";
	case HOTKEY_A_SKILLF_1_5: return "Alt. Skill 5 (Bottom bar)";
	case HOTKEY_A_SKILLF_1_6: return "Alt. Skill 6 (Bottom bar)";
	case HOTKEY_A_SKILLF_1_7: return "Alt. Skill 7 (Bottom bar)";
	case HOTKEY_A_SKILLF_1_8: return "Alt. Skill 8 (Bottom bar)";
	case HOTKEY_A_SKILLF_2_1: return "Alt. Skill 1 (Middle bar)";
	case HOTKEY_A_SKILLF_2_2: return "Alt. Skill 2 (Middle bar)";
	case HOTKEY_A_SKILLF_2_3: return "Alt. Skill 3 (Middle bar)";
	case HOTKEY_A_SKILLF_2_4: return "Alt. Skill 4 (Middle bar)";
	case HOTKEY_A_SKILLF_2_5: return "Alt. Skill 5 (Middle bar)";
	case HOTKEY_A_SKILLF_2_6: return "Alt. Skill 6 (Middle bar)";
	case HOTKEY_A_SKILLF_2_7: return "Alt. Skill 7 (Middle bar)";
	case HOTKEY_A_SKILLF_2_8: return "Alt. Skill 8 (Middle bar)";
	case HOTKEY_A_SKILLF_3_1: return "Alt. Skill 1 (Top bar)";
	case HOTKEY_A_SKILLF_3_2: return "Alt. Skill 2 (Top bar)";
	case HOTKEY_A_SKILLF_3_3: return "Alt. Skill 3 (Top bar)";
	case HOTKEY_A_SKILLF_3_4: return "Alt. Skill 4 (Top bar)";
	case HOTKEY_A_SKILLF_3_5: return "Alt. Skill 5 (Top bar)";
	case HOTKEY_A_SKILLF_3_6: return "Alt. Skill 6 (Top bar)";
	case HOTKEY_A_SKILLF_3_7: return "Alt. Skill 7 (Top bar)";
	case HOTKEY_A_SKILLF_3_8: return "Alt. Skill 8 (Top bar)";
	case HOTKEY_A_PETSKILL_1: return "Pet Normal Attack";
	case HOTKEY_A_PETSKILL_2: return "Pet Skill 1";
	case HOTKEY_A_PETSKILL_3: return "Pet Skill 2";
	case HOTKEY_A_PETSKILL_4: return "Pet Skill 3";
	case HOTKEY_A_PETSKILL_5: return "Pet Skill 4";
	case HOTKEY_A_PETSKILL_6: return "Pet Skill 5";
	case HOTKEY_A_PETSKILL_7: return "Pet Skill 6";
	case HOTKEY_A_PETSKILL_8: return "Pet Skill 7";
	case HOTKEY_A_PETSKILL_9: return "Pet Skill 8";
	case HOTKEY_A_PET_MODE_FOLLOW: return "Set Pet Follow Mode";
	case HOTKEY_A_PET_MODE_STOP: return "Set Pet Stop Mode";
	case HOTKEY_A_HIDEUI: return "Toggle Hide UI";
	case HOTKEY_A_CONSOLE: return "Show/hide console";
	case HOTKEY_A_ROLL: return "Make a roll";
	case HOTKEY_A_MAP: return "Show/hide map";
	case HOTKEY_A_CHARACTER: return "Show/hide character window";
	case HOTKEY_A_INVENTORY: return "Show/hide inventory";
	case HOTKEY_A_SKILLS: return "Show/hide skill window";
	case HOTKEY_A_QUESTS: return "Show/hide quest window";
	case HOTKEY_A_ACTIONS: return "Show/hide action window";
	case HOTKEY_A_PARTY: return "Show/hide party window";
	case HOTKEY_A_FRIENDS: return "Show/hide friends window";
	case HOTKEY_A_FACTION: return "Show/hide faction window";
	case HOTKEY_A_PETS: return "Show/hide pet window";
	case HOTKEY_A_HELP: return "Show/hide help window";
	case HOTKEY_A_GM_CONSOLE: return "Show/hide GM console";
	case HOTKEY_A_SELECT_MOB: return "Select nearest mob";
	default: break;
	}

	return "Unknown";
}

static const char *g_keynames[] = {
	"Unknown 0x00",
	"LMB",
	"RMB",
	"Cancel",
	"MMB",
	"XMB1",
	"XMB2",
	"Unknown 0x07",
	"Backspace",
	"Tab",
	"Unknown 0x0A",
	"Unknown 0x0B",
	"Clear",
	"Enter",
	"Unknown 0x0E",
	"Unknown 0x0F",
	"Shift", /* 0x10 */
	"Control",
	"Alt",
	"Pause",
	"Capslock",
	"IME Kana",
	"Unknown 0x16",
	"IME Junja",
	"IME Final",
	"IME Kanji",
	"Unknown 0x1A",
	"Escape",
	"IME Convert",
	"IME Nonconvert",
	"IME Accept",
	"IME mode change",
	"Space", /* 0x20 */
	"Page Up",
	"Page Down",
	"End",
	"Home",
	"Left arrow",
	"Up arrow",
	"Right arrow",
	"Down arrow",
	"Select",
	"Print",
	"Execute",
	"Print Screen",
	"Insert",
	"Delete",
	"Help",
	"0",  /* 0x30 */
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"Unknown 0x3A",
	"Unknown 0x3B",
	"Unknown 0x3C",
	"Unknown 0x3D",
	"Unknown 0x3E",
	"Unknown 0x3F",
	"Unknown 0x40",  /* 0x40 */
	"A",
	"B",
	"C",
	"D",
	"E",
	"F",
	"G",
	"H",
	"I",
	"J",
	"K",
	"L",
	"M",
	"N",
	"O",
	"P",  /* 0x50 */
	"Q",
	"R",
	"S",
	"T",
	"U",
	"V",
	"W",
	"X",
	"Y",
	"Z",
	"Left Win",
	"Right Win",
	"App key",
	"Unknown 0x5E",
	"Sleep",
	"Num 0",  /* 0x60 */
	"Num 1",
	"Num 2",
	"Num 3",
	"Num 4",
	"Num 5",
	"Num 6",
	"Num 7",
	"Num 8",
	"Num 9",
	"Num *",
	"Num +",
	"Separator",
	"Num -",
	"Num ,",
	"Num /",
	"F1",  /* 0x70 */
	"F2",
	"F3",
	"F4",
	"F5",
	"F6",
	"F7",
	"F8",
	"F9",
	"F10",
	"F11",
	"F12",
	"F13",
	"F14",
	"F15",
	"F16",
	"F17",  /* 0x80 */
	"F18",
	"F19",
	"F20",
	"F21",
	"F22",
	"F23",
	"F24",
	"Unknown 0x88",
	"Unknown 0x89",
	"Unknown 0x8A",
	"Unknown 0x8B",
	"Unknown 0x8C",
	"Unknown 0x8D",
	"Unknown 0x8E",
	"Unknown 0x8F",
	"Numlock",  /* 0x90 */
	"Scroll Lock",
	"Special 1",
	"Special 2",
	"Special 3",
	"Special 4",
	"Special 5",
	"Unknown 0x97",
	"Unknown 0x98",
	"Unknown 0x99",
	"Unknown 0x9A",
	"Unknown 0x9B",
	"Unknown 0x9C",
	"Unknown 0x9D",
	"Unknown 0x9E",
	"Unknown 0x9F",
	"Left Shift", /* 0xA0 */
	"Right Shift",
	"Left Control",
	"Right Control",
	"Left Alt",
	"Right Alt",
	"Back",
	"Forward",
	"Refresh",
	"Stop",
	"Search",
	"Favorites",
	"Home",
	"Mute",
	"Vol. Down",
	"Vol. Up",
	"Next Track", /* 0xB0 */
	"Prev. Track",
	"Stop",
	"Play/Pause",
	"Mail",
	"Media",
	"Custom 1",
	"Custom 2",
	"Unknown 0xB8",
	"Unknown 0xB9",
	";",
	"+",
	",",
	"-",
	".",
	"/",
	"~", /* 0xC0 */
	"Unknown 0xC1",
	"Unknown 0xC2",
	"Unknown 0xC3",
	"Unknown 0xC4",
	"Unknown 0xC5",
	"Unknown 0xC6",
	"Unknown 0xC7",
	"Unknown 0xC8",
	"Unknown 0xC9",
	"Unknown 0xCA",
	"Unknown 0xCB",
	"Unknown 0xCC",
	"Unknown 0xCD",
	"Unknown 0xCE",
	"Unknown 0xCF",
	"Unknown 0xD0", /* 0xD0 */
	"Unknown 0xD1",
	"Unknown 0xD2",
	"Unknown 0xD3",
	"Unknown 0xD4",
	"Unknown 0xD5",
	"Unknown 0xD6",
	"Unknown 0xD7",
	"Unknown 0xD8",
	"Unknown 0xD9",
	"Unknown 0xDA",
	"[",
	"\\",
	"]",
	"'",
	"Misc",
	"Unknown 0xE0", /* 0xE0 */
	"Unknown 0xE1",
	"\\",
	"Unknown 0xE3",
	"Unknown 0xE4",
	"IME Process",
	"Unknown 0xE6",
	"Unknown (Packet)",
	"Unknown 0xE8",
	"Unknown 0xE9",
	"Unknown 0xEA",
	"Unknown 0xEB",
	"Unknown 0xEC",
	"Unknown 0xED",
	"Unknown 0xEE",
	"Unknown 0xEF",
	"Unknown 0xF0", /* 0xF0 */
	"Unknown 0xF1",
	"Unknown 0xF2",
	"Unknown 0xF3",
	"Unknown 0xF4",
	"Unknown 0xF5",
	"Attn",
	"CrSel",
	"ExSel",
	"Erase EOF",
	"Play",
	"Zoom",
	"Unknown 0xFC",
	"PA1",
	"Clear",
	"Unknown 0xFF",
	"Num Enter", /* 0x100 */
	"Num Left",
	"Num Up",
	"Num Right",
	"Num Down",
	"Num End",
	"Num Home",
	"Num Page Up",
	"Num Page Down",
	"Num Insert",
	"Num Delete",
};

const char *
mg_input_to_str(int key)
{
	if (key >= sizeof(g_keynames) / sizeof(g_keynames[0])) {
		return "Unknown";
	}

	return g_keynames[key];
}

int
mg_winevent_to_event(WPARAM wparam, LPARAM lparam)
{
	bool is_altrn = (lparam >> 24) & 1;
	switch (wparam) {
		case VK_RETURN: return is_altrn ? 0x100 : VK_RETURN;
		case VK_LEFT: return is_altrn ? VK_LEFT : 0x101;
		case VK_UP: return is_altrn ? VK_UP : 0x102;
		case VK_RIGHT: return is_altrn ? VK_RIGHT : 0x103;
		case VK_DOWN: return is_altrn ? VK_DOWN : 0x104;
		case VK_END: return is_altrn ? VK_END : 0x105;
		case VK_HOME: return is_altrn ? VK_HOME : 0x106;
		case VK_PRIOR: return is_altrn ? VK_PRIOR : 0x107;
		case VK_NEXT: return is_altrn ? VK_NEXT : 0x108;
		case VK_INSERT: return is_altrn ? VK_INSERT : 0x109;
		case VK_DELETE: return is_altrn ? VK_DELETE : 0x10A;
		case VK_LSHIFT: case VK_RSHIFT: return VK_SHIFT;
		case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
		case VK_LMENU: case VK_RMENU: return VK_MENU;
		default: break;
	}

	return wparam;
}

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

static enum mg_input_action
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
	enum mg_input_action action;

	if (g_pw_data->game->player->is_in_cosmetician) {
		return false;
	}

	keycode = mg_winevent_to_event(keycode, mods);
	action = get_mapped_action(event, keycode);
	pw_log("event=%x, keycode=%x, mods=%x => %d", event, keycode, mods, action);
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
	case HOTKEY_A_HIDEUI:
		if (!is_repeat) {
			ui_man->show_ui = !ui_man->show_ui;
			g_disable_all_overlay = !ui_man->show_ui;
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

/** strip preceeding and following quotes and whitespaces */
static char *
cleanup_str(char *str, const char *chars_to_remove)
{
	int len = strlen(str);
	char *end = str + len - 1;

	while (*str) {
		if (*str == '\\' && *(str + 1) == '"') {
			str += 2;
			continue;
		} else if (strchr(chars_to_remove, *str)) {
			*str = 0;
			str++;
			continue;
		}
		break;
	}

	while (end > str) {
		if (*(end - 1) == '\\' && *end == '"') {
			end -= 2;
			continue;
		} else if (strchr(chars_to_remove, *end)) {
			*end = 0;
			end--;
			continue;
		}
		break;
	}

	return str;
}

static const char *
bind_cmd_handler(const char *val, void *ctx)
{
	static char res_buf[168];
	char buf[128];
	char *c;
	char *words[3];
	int wordcnt = sizeof(words) / sizeof(words[0]);
	int hotkey_id, action_id;
	union hotkey_mod_mask mods = {0};
	int rc;

	snprintf(buf, sizeof(buf), "%s", val);
	rc = split_string_to_words(buf, words, &wordcnt);

	if (rc != 0 || wordcnt != 2) {
		return "^ff0000Usage: bind <hotkeys> <action>";
	}

	if (strstr(words[0], "Ctrl ")) {
		mods.ctrl = 1;
	}

	if (strstr(words[0], "Shift ")) {
		mods.shift = 1;
	}

	if (strstr(words[0], "Alt ")) {
		mods.alt = 1;
	}

	c = strrchr(words[0], '+');
	if (!c) {
		c = words[0];
	}
	c = cleanup_str(c, " ");
	for (hotkey_id = 0; hotkey_id < sizeof(g_keynames) / sizeof(g_keynames[0]); hotkey_id++) {
		if (strcmp(c, g_keynames[hotkey_id]) == 0) {
			break;
		}
	}

	if (hotkey_id == sizeof(g_keynames) / sizeof(g_keynames[0])) {
		snprintf(res_buf, sizeof(res_buf), "^ff0000Unknown hotkey: \"%s\"", c);
		return res_buf;
	}

	for (action_id = 1; action_id < HOTKEY_A_MAX; action_id++) {
		if (strcmp(words[1], mg_input_action_to_str(action_id)) == 0) {
			break;
		}
	}

	if (action_id == HOTKEY_A_MAX) {
		snprintf(res_buf, sizeof(res_buf), "^ff0000Unknown action: \"%s\"", words[1]);
		return res_buf;
	}

	pw_log("setting \"%s\" to \"%d\"", words[0], hotkey_id);
	_set_key((struct hotkey){ .key = hotkey_id, .action = action_id, .mods = mods });
	return "";
}

CSH_REGISTER_CMD("bind", bind_cmd_handler);
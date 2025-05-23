#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <io/pad.h>
#include <sys/process.h>

#include "sfo.h"
#include "saves.h"
#include "common.h"
#include "menu.h"
#include "menu_gui.h"
#include "ttf_render.h"
#include "ps1card.h"
#include "mcio.h"

#include <tiny3d.h>
#include <libfont.h>

extern save_list_t hdd_saves;
extern save_list_t usb_saves;
extern save_list_t trophies;
extern save_list_t online_saves;
extern save_list_t user_backup;
extern save_list_t vmc1_saves;
extern save_list_t vmc2_saves;

extern int close_app;
extern padData paddata[];

int menu_options_maxopt = 0;
int * menu_options_maxsel;

int menu_id = 0;												// Menu currently in
int menu_sel = 0;												// Index of selected item (use varies per menu)
int menu_old_sel[TOTAL_MENU_IDS] = { 0 };						// Previous menu_sel for each menu
int last_menu_id[TOTAL_MENU_IDS] = { 0 };						// Last menu id called (for returning)

save_entry_t* selected_entry;
code_entry_t* selected_centry;
int option_index = 0;
static hexedit_data_t hex_data;

void initMenuOptions(void)
{
	menu_options_maxopt = 0;
	while (menu_options[menu_options_maxopt].name)
		menu_options_maxopt++;

	menu_options_maxsel = (int *)calloc(menu_options_maxopt, sizeof(int));

	for (int i = 0; i < menu_options_maxopt; i++)
	{
		if (menu_options[i].type == APP_OPTION_LIST)
		{
			while (menu_options[i].options[menu_options_maxsel[i]])
				menu_options_maxsel[i]++;
		}
	}
}

static void LoadFileTexture(const char* fname)
{
	pngLoadFromFile(fname, &menu_textures[icon_png_file_index].texture);
	copyTexture(icon_png_file_index);

	menu_textures[icon_png_file_index].size = 1;
	free_mem = (u32*) menu_textures[icon_png_file_index].buffer;
}

static void LoadVmcTexture(int width, int height, uint8_t* icon)
{
	menu_textures[icon_png_file_index].texture.width = width;
	menu_textures[icon_png_file_index].texture.height = height;
	menu_textures[icon_png_file_index].texture.pitch = (width*4);
	menu_textures[icon_png_file_index].texture.bmp_out = icon;
	copyTexture(icon_png_file_index);

	menu_textures[icon_png_file_index].size = 1;
	free_mem = (u32*) menu_textures[icon_png_file_index].buffer;
}

static int ReloadUserSaves(save_list_t* save_list)
{
	init_loading_screen("Loading save games...");

	if (save_list->list)
	{
		UnloadGameList(save_list->list);
		save_list->list = NULL;
	}

	if (save_list->UpdatePath)
		save_list->UpdatePath(save_list->path);

	save_list->list = save_list->ReadList(save_list->path);
	if (apollo_config.doSort == SORT_BY_NAME)
		list_bubbleSort(save_list->list, &sortSaveList_Compare);
	else if (apollo_config.doSort == SORT_BY_TITLE_ID)
		list_bubbleSort(save_list->list, &sortSaveList_Compare_TitleID);
	else if (apollo_config.doSort == SORT_BY_TYPE)
		list_bubbleSort(save_list->list, &sortSaveList_Compare_Type);

	stop_loading_screen();

	if (!save_list->list)
	{
		show_message("No save-games found");
		return 0;
	}

	return list_count(save_list->list);
}

static code_entry_t* LoadRawPatch(void)
{
	char patchPath[256];
	code_entry_t* centry = calloc(1, sizeof(code_entry_t));

	centry->name = strdup(selected_entry->title_id);
	snprintf(patchPath, sizeof(patchPath), APOLLO_DATA_PATH "%s.savepatch", selected_entry->title_id);
	centry->codes = readTextFile(patchPath, NULL);

	return centry;
}

static code_entry_t* LoadSaveDetails(const save_entry_t* save)
{
	char sfoPath[256];
	code_entry_t* centry = calloc(1, sizeof(code_entry_t));

	centry->name = strdup(save->title_id);

	if(save->type == FILE_TYPE_PS1)
	{
		asprintf(&centry->codes, "%s\n\n----- PS1 Save -----\n"
			"Game: %s\n"
			"Title ID: %s\n"
			"File: %s\n",
			save->path,
			save->name,
			save->title_id,
			save->dir_name);
		return(centry);
	}

	if(save->type == FILE_TYPE_PS2)
	{
		asprintf(&centry->codes, "%s\n\n----- PS2 Save -----\n"
			"Game: %s\n"
			"Title ID: %s\n"
			"Folder: %s\n"
			"Icon: %s\n",
			save->path,
			save->name,
			save->title_id,
			save->dir_name,
			strrchr(save->path, '\n')+1);
		return(centry);
	}

	if(save->type == FILE_TYPE_VMC)
	{
		asprintf(&centry->codes, "%s\n\n----- Virtual Memory Card -----\n"
			"File: %s\n"
			"Folder: %s\n",
			save->path,
			strrchr(save->path, '/')+1,
			save->dir_name);
		return(centry);
	}

	if (save->flags & SAVE_FLAG_ONLINE)
	{
		asprintf(&centry->codes, "%s\n----- Online Database -----\n"
			"Game: %s\n"
			"Title ID: %s\n",
			save->path,
			save->name,
			save->title_id);
		return(centry);
	}

	if (!(save->flags & SAVE_FLAG_PS3))
	{
		asprintf(&centry->codes, "%s\n\nTitle: %s\n", save->path, save->name);
		return(centry);
	}

	snprintf(sfoPath, sizeof(sfoPath), "%s" "PARAM.SFO", save->path);
	LOG("Save Details :: Reading %s...", sfoPath);

	sfo_context_t* sfo = sfo_alloc();
	if (sfo_read(sfo, sfoPath) < 0) {
		LOG("Unable to read from '%s'", sfoPath);
		sfo_free(sfo);
		return centry;
	}

	if (save->flags & SAVE_FLAG_TROPHY)
	{
		char* account = (char*) sfo_get_param_value(sfo, "ACCOUNTID");
		asprintf(&centry->codes, "%s\n\n"
			"Title: %s\n"
			"NP Comm ID: %s\n"
			"Account ID: %.16s\n", save->path, save->name, save->title_id, account);
		LOG(centry->codes);

		sfo_free(sfo);
		return(centry);
	}

	char* subtitle = (char*) sfo_get_param_value(sfo, "SUB_TITLE");
	sfo_params_ids_t* param_ids = (sfo_params_ids_t*)(sfo_get_param_value(sfo, "PARAMS") + 0x1C);
	param_ids->user_id = ES32(param_ids->user_id);

	asprintf(&centry->codes, "%s\n\n"
		"Title: %s\n"
		"Sub-Title: %s\n"
		"Folder: %s\n"
		"Lock: %s\n\n"
		"User ID: %08d\n"
		"Account ID: %.16s (%s)\n"
		"PSID: %016lX %016lX\n", save->path, save->name, subtitle, 
		save->dir_name,
		(save->flags & SAVE_FLAG_LOCKED ? "Copying Prohibited" : "Unlocked"),
		param_ids->user_id, param_ids->account_id, 
		(save->flags & SAVE_FLAG_OWNER ? "Owner" : "Not Owner"),
		param_ids->psid[0], param_ids->psid[1]);
	LOG(centry->codes);

	sfo_free(sfo);
	return (centry);
}

static void SetMenu(int id)
{
	switch (menu_id) //Leaving menu
	{
		case MENU_PS1VMC_SAVES:
			if (id == MENU_MAIN_SCREEN)
			{
				UnloadGameList(vmc1_saves.list);
				vmc1_saves.list = NULL;
			}
			break;

		case MENU_PS2VMC_SAVES:
			if (id == MENU_MAIN_SCREEN)
			{
				init_loading_screen("Saving PS2 Memory Card...");
				UnloadGameList(vmc2_saves.list);
				vmc2_saves.list = NULL;
				mcio_vmcFinish();
				stop_loading_screen();
			}
			break;

		case MENU_ONLINE_DB: //Cheats Online Menu
			if (apollo_config.online_opt && id == MENU_MAIN_SCREEN)
			{
				UnloadGameList(online_saves.list);
				online_saves.list = NULL;
			}
			break;

		case MENU_MAIN_SCREEN: //Main Menu
		case MENU_TROPHIES:
		case MENU_USB_SAVES: //USB Saves Menu
		case MENU_HDD_SAVES: //HHD Saves Menu
		case MENU_USER_BACKUP: //Backup Menu
			menu_textures[icon_png_file_index].size = 0;
			break;

		case MENU_SETTINGS: //Options Menu
		case MENU_CREDITS: //About Menu
			break;

		case MENU_PATCHES: //Cheat Selection Menu
			if (selected_entry->flags & SAVE_FLAG_UPDATED)
			{
				switch (id)
				{
				case MENU_PS1VMC_SAVES:
					saveMemoryCard(vmc1_saves.path, 0, 0);
					ReloadUserSaves(&vmc1_saves);
					break;
				
				case MENU_PS2VMC_SAVES:
					ReloadUserSaves(&vmc2_saves);
					break;

				case MENU_USB_SAVES:
					ReloadUserSaves(&usb_saves);
					break;
				}

				selected_entry->flags ^= SAVE_FLAG_UPDATED;
			}
			break;

		case MENU_SAVE_DETAILS:
		case MENU_PATCH_VIEW: //Cheat View Menu
			if (apollo_config.doAni)
				Draw_CheatsMenu_View_Ani_Exit();
			break;

		case MENU_CODE_OPTIONS: //Cheat Option Menu
			if (apollo_config.doAni)
				Draw_CheatsMenu_Options_Ani_Exit();
			break;

		case MENU_HEX_EDITOR: //Hex Editor Menu
			//(hack) restore patch menu prev-id
			last_menu_id[MENU_PATCHES] = last_menu_id[MENU_HEX_EDITOR];
			last_menu_id[menu_id] = id;
			break;
	}
	
	switch (id) //going to menu
	{
		case MENU_MAIN_SCREEN: //Main Menu
			if (apollo_config.doAni || menu_id == MENU_MAIN_SCREEN) //if load animation
				Draw_MainMenu_Ani();
			break;

		case MENU_TROPHIES: //Trophies Menu
			if (!trophies.list && !ReloadUserSaves(&trophies))
				return;

			if (apollo_config.doAni)
				Draw_UserCheatsMenu_Ani(&trophies);
			break;

		case MENU_USB_SAVES: //USB saves Menu
			if (!usb_saves.list && !ReloadUserSaves(&usb_saves))
				return;
			
			if (apollo_config.doAni)
				Draw_UserCheatsMenu_Ani(&usb_saves);
			break;

		case MENU_HDD_SAVES: //HDD saves Menu
			if (!hdd_saves.list && !ReloadUserSaves(&hdd_saves))
				return;
			
			if (apollo_config.doAni)
				Draw_UserCheatsMenu_Ani(&hdd_saves);
			break;

		case MENU_ONLINE_DB: //Cheats Online Menu
			if (apollo_config.online_opt && online_saves.list && menu_id == MENU_MAIN_SCREEN)
			{
				UnloadGameList(online_saves.list);
				online_saves.list = NULL;
			}

			if (!online_saves.list && !ReloadUserSaves(&online_saves))
				return;

			if (apollo_config.doAni)
				Draw_UserCheatsMenu_Ani(&online_saves);
			break;

		case MENU_PS1VMC_SAVES: //VMC Menu
			if (!vmc1_saves.list && !ReloadUserSaves(&vmc1_saves))
				return;

			if (apollo_config.doAni)
				Draw_UserCheatsMenu_Ani(&vmc1_saves);
			break;

		case MENU_PS2VMC_SAVES: //Trophies Menu
			if (!vmc2_saves.list && !ReloadUserSaves(&vmc2_saves))
				return;

			if (apollo_config.doAni)
				Draw_UserCheatsMenu_Ani(&vmc2_saves);
			break;

		case MENU_CREDITS: //About Menu
			if (apollo_config.doAni)
				Draw_AboutMenu_Ani();
			break;

		case MENU_SETTINGS: //Options Menu
			if (apollo_config.doAni)
				Draw_OptionsMenu_Ani();
			break;

		case MENU_USER_BACKUP: //User Backup Menu
			if (!user_backup.list)
				ReloadUserSaves(&user_backup);

			if (apollo_config.doAni)
				Draw_UserCheatsMenu_Ani(&user_backup);
			break;

		case MENU_PATCHES: //Cheat Selection Menu
			//if entering from game list, don't keep index, otherwise keep
			if (menu_id == MENU_USB_SAVES || menu_id == MENU_HDD_SAVES || menu_id == MENU_ONLINE_DB || 
				menu_id == MENU_TROPHIES || menu_id == MENU_PS1VMC_SAVES || menu_id == MENU_PS2VMC_SAVES)
				menu_old_sel[MENU_PATCHES] = 0;

			char iconfile[256];
			menu_textures[icon_png_file_index].size = 0;
			snprintf(iconfile, sizeof(iconfile), "%s" "ICON0.PNG", selected_entry->path);

			if (selected_entry->flags & SAVE_FLAG_ONLINE)
			{
				snprintf(iconfile, sizeof(iconfile), APOLLO_LOCAL_CACHE "%s.PNG", selected_entry->title_id);

				if (file_exists(iconfile) != SUCCESS)
					http_download(selected_entry->path, (selected_entry->flags & SAVE_FLAG_PS3) ? "ICON0.PNG" : "icon0.png", iconfile, 1);
			}

			if (selected_entry->flags & SAVE_FLAG_VMC && selected_entry->type == FILE_TYPE_PS1)
				LoadVmcTexture(16, 16, getIconRGBA(selected_entry->blocks, 0));

			if (selected_entry->flags & SAVE_FLAG_VMC && selected_entry->type == FILE_TYPE_PS2)
				LoadVmcTexture(128, 128, getIconPS2(selected_entry->dir_name, strrchr(selected_entry->path, '\n')+1));

			if (file_exists(iconfile) == SUCCESS)
				LoadFileTexture(iconfile);

			if (apollo_config.doAni && menu_id != MENU_PATCH_VIEW && menu_id != MENU_CODE_OPTIONS)
				Draw_CheatsMenu_Selection_Ani();
			break;

		case MENU_PATCH_VIEW: //Cheat View Menu
			menu_old_sel[MENU_PATCH_VIEW] = 0;
			if (apollo_config.doAni)
				Draw_CheatsMenu_View_Ani("Patch view");
			break;

		case MENU_SAVE_DETAILS: //Save Detail View Menu
			if (apollo_config.doAni)
				Draw_CheatsMenu_View_Ani(selected_entry->name);
			break;

		case MENU_CODE_OPTIONS: //Cheat Option Menu
			menu_old_sel[MENU_CODE_OPTIONS] = 0;
			if (apollo_config.doAni)
				Draw_CheatsMenu_Options_Ani();
			break;

		case MENU_HEX_EDITOR: //Hex Editor Menu
			//(hack) save patch menu prev-id
			last_menu_id[MENU_HEX_EDITOR] = last_menu_id[MENU_PATCHES];
			last_menu_id[menu_id] = id;

			if (apollo_config.doAni)
				Draw_HexEditor_Ani(&hex_data);
			break;
	}
	
	menu_old_sel[menu_id] = menu_sel;
	if (last_menu_id[menu_id] != id)
		last_menu_id[id] = menu_id;
	menu_id = id;
	
	menu_sel = menu_old_sel[menu_id];
}

static void move_selection_back(int game_count, int steps)
{
	menu_sel -= steps;
	if ((menu_sel == -1) && (steps == 1))
		menu_sel = game_count - 1;
	else if (menu_sel < 0)
		menu_sel = 0;
}

static void move_selection_fwd(int game_count, int steps)
{
	menu_sel += steps;
	if ((menu_sel == game_count) && (steps == 1))
		menu_sel = 0;
	else if (menu_sel >= game_count)
		menu_sel = game_count - 1;
}

static int updatePadSelection(int total)
{
	if(paddata[0].BTN_UP)
		move_selection_back(total, 1);

	else if(paddata[0].BTN_DOWN)
		move_selection_fwd(total, 1);

	else if (paddata[0].BTN_LEFT)
		move_selection_back(total, 5);

	else if (paddata[0].BTN_L1)
		move_selection_back(total, 25);

	else if (paddata[0].BTN_L2)
		menu_sel = 0;

	else if (paddata[0].BTN_RIGHT)
		move_selection_fwd(total, 5);

	else if (paddata[0].BTN_R1)
		move_selection_fwd(total, 25);

	else if (paddata[0].BTN_R2)
		menu_sel = total - 1;

	else return 0;

	return 1;
}

static void doSaveMenu(save_list_t * save_list)
{
	if(readPad(0))
	{
		if (updatePadSelection(list_count(save_list->list)))
			(void)0;
	
		else if (paddata[0].BTN_CIRCLE)
		{
			SetMenu(MENU_MAIN_SCREEN);
			return;
		}
		else if (paddata[0].BTN_CROSS)
		{
			selected_entry = list_get_item(save_list->list, menu_sel);

			if (selected_entry->type == FILE_TYPE_VMC && selected_entry->flags & SAVE_FLAG_VMC)
			{
				if (selected_entry->flags & SAVE_FLAG_PS1)
				{
					strncpy(vmc1_saves.path, selected_entry->path, sizeof(vmc1_saves.path));
					SetMenu(MENU_PS1VMC_SAVES);
				}
				else
				{
					strncpy(vmc2_saves.path, selected_entry->path, sizeof(vmc2_saves.path));
					SetMenu(MENU_PS2VMC_SAVES);
				}

				return;
			}

			if (!selected_entry->codes && !save_list->ReadCodes(selected_entry))
			{
				show_message("No data found in folder:\n%s", selected_entry->path);
				return;
			}

			if (apollo_config.doSort && 
				((save_list->id == MENU_USER_BACKUP) || (save_list->id == MENU_ONLINE_DB)))
				list_bubbleSort(selected_entry->codes, &sortCodeList_Compare);

			SetMenu(MENU_PATCHES);
			return;
		}
		else if (paddata[0].BTN_TRIANGLE && save_list->UpdatePath)
		{
			selected_entry = list_get_item(save_list->list, menu_sel);
			if (selected_entry->type != FILE_TYPE_MENU)
			{
				selected_centry = LoadSaveDetails(selected_entry);
				SetMenu(MENU_SAVE_DETAILS);
				return;
			}
		}
		else if (paddata[0].BTN_SELECT)
		{
			selected_entry = list_get_item(save_list->list, menu_sel);
			if (save_list->id != MENU_ONLINE_DB && save_list->id != MENU_USER_BACKUP &&
				selected_entry->type != FILE_TYPE_MENU && selected_entry->type != FILE_TYPE_VMC &&
				(selected_entry->flags & (SAVE_FLAG_PS3|SAVE_FLAG_PS2|SAVE_FLAG_PS1)))
				selected_entry->flags ^= SAVE_FLAG_SELECTED;
		}
		else if (paddata[0].BTN_SQUARE)
		{
			ReloadUserSaves(save_list);
		}
	}

	Draw_UserCheatsMenu(save_list, menu_sel, 0xFF);
}

static void doMainMenu(void)
{
	// Check the pads.
	if (readPad(0))
	{
		if(paddata[0].BTN_LEFT)
			move_selection_back(MENU_CREDITS, 1);

		else if(paddata[0].BTN_RIGHT)
			move_selection_fwd(MENU_CREDITS, 1);

		else if (paddata[0].BTN_CROSS)
			SetMenu(menu_sel+1);

		else if (paddata[0].BTN_L1 && paddata[0].BTN_START && (file_exists(APOLLO_PATH "EXTRA.SELF") == SUCCESS))
			sysProcessExitSpawn2(APOLLO_PATH "EXTRA.SELF", NULL, NULL, NULL, 0, 1001, SYS_PROCESS_SPAWN_STACK_SIZE_1M);

		else if(paddata[0].BTN_CIRCLE && show_dialog(DIALOG_TYPE_YESNO, "Exit to XMB?"))
			close_app = 1;
	}
	
	Draw_MainMenu();
}

void doAboutMenu(void)
{
	// Check the pads.
	if (readPad(0))
	{
		if (paddata[0].BTN_CIRCLE)
		{
			SetMenu(MENU_MAIN_SCREEN);
			return;
		}
		else if (paddata[0].BTN_SELECT)
		{
			int m = !apollo_config.music;

			music_callback(0);
			Draw_AboutMenuLL();
			music_callback(m);

			SetMenu(MENU_MAIN_SCREEN);
			return;
		}
	}

	Draw_AboutMenu();
}

static void doOptionsMenu(void)
{
	// Check the pads.
	if (readPad(0))
	{
		if(paddata[0].BTN_UP)
			move_selection_back(menu_options_maxopt, 1);

		else if(paddata[0].BTN_DOWN)
			move_selection_fwd(menu_options_maxopt, 1);

		else if (paddata[0].BTN_CIRCLE)
		{
			save_app_settings(&apollo_config);
			set_ttf_window(0, 0, 848, 512, WIN_SKIP_LF);
			SetMenu(MENU_MAIN_SCREEN);
			return;
		}
		else if (paddata[0].BTN_LEFT)
		{
			if (menu_options[menu_sel].type == APP_OPTION_LIST)
			{
				if (*menu_options[menu_sel].value > 0)
					(*menu_options[menu_sel].value)--;
				else
					*menu_options[menu_sel].value = menu_options_maxsel[menu_sel] - 1;
			}
			else if (menu_options[menu_sel].type == APP_OPTION_INC)
				(*menu_options[menu_sel].value)--;
			
			if (menu_options[menu_sel].type != APP_OPTION_CALL)
				menu_options[menu_sel].callback(*menu_options[menu_sel].value);
		}
		else if (paddata[0].BTN_RIGHT)
		{
			if (menu_options[menu_sel].type == APP_OPTION_LIST)
			{
				if (*menu_options[menu_sel].value < (menu_options_maxsel[menu_sel] - 1))
					*menu_options[menu_sel].value += 1;
				else
					*menu_options[menu_sel].value = 0;
			}
			else if (menu_options[menu_sel].type == APP_OPTION_INC)
				*menu_options[menu_sel].value += 1;

			if (menu_options[menu_sel].type != APP_OPTION_CALL)
				menu_options[menu_sel].callback(*menu_options[menu_sel].value);
		}
		else if (paddata[0].BTN_CROSS)
		{
			if (menu_options[menu_sel].type == APP_OPTION_BOOL)
				menu_options[menu_sel].callback(*menu_options[menu_sel].value);

			else if (menu_options[menu_sel].type == APP_OPTION_CALL)
				menu_options[menu_sel].callback(0);
		}
	}
	
	Draw_OptionsMenu();
}

static void doHexEditor(void)
{
	// Check the pads.
	if (readPad(0))
	{
		if(paddata[0].BTN_UP)
		{
			if (hex_data.pos >= 0x10)
				hex_data.pos -= 0x10;
		}

		else if(paddata[0].BTN_DOWN)
		{
			if (hex_data.pos + 0x10 < hex_data.size)
				hex_data.pos += 0x10;
		}
		else if (paddata[0].BTN_LEFT)
		{
			if (hex_data.low_nibble)
				hex_data.low_nibble ^= 1;

			else if (hex_data.pos > 0)
			{
				hex_data.pos--;
				hex_data.low_nibble ^= 1;
			}
		}
		else if (paddata[0].BTN_RIGHT)
		{
			if (!hex_data.low_nibble)
				hex_data.low_nibble ^= 1;

			else if (hex_data.pos + 1 < hex_data.size)
			{
				hex_data.pos++;
				hex_data.low_nibble ^= 1;
			}
		}
		else if (paddata[0].BTN_L1)
		{
			hex_data.pos -= 0x120;
			if (hex_data.pos < 0)
				hex_data.pos = 0;
		}
		else if (paddata[0].BTN_R1)
		{
			if (hex_data.pos + 0x120 < hex_data.size)
				hex_data.pos += 0x120;
		}
		else if (paddata[0].BTN_L2)
			hex_data.pos = 0;

		else if (paddata[0].BTN_R2)
			hex_data.pos = hex_data.size - 1;

		else if (paddata[0].BTN_CIRCLE)
		{
			if (show_dialog(DIALOG_TYPE_YESNO, "Save changes to %s?", strrchr(hex_data.filepath, '/') + 1) &&
				(write_buffer(hex_data.filepath, hex_data.data, hex_data.size) == SUCCESS))
			{
				option_value_t* optval = list_get_item(selected_centry->options[option_index].opts, menu_sel);
				optval->value[1] = CMD_IMPORT_DATA_FILE;
				execCodeCommand(selected_centry, optval->value+1);
			}
			free(hex_data.data);

			SetMenu(MENU_PATCHES);
			return;
		}
		else if (paddata[0].BTN_CROSS)
		{
			if ((hex_data.data[hex_data.pos] & (0xF0 >> hex_data.low_nibble * 4)) == (0xF0 >> hex_data.low_nibble * 4))
				hex_data.data[hex_data.pos] &= (0x0F << hex_data.low_nibble * 4);
			else
				hex_data.data[hex_data.pos] += (0x10 >> hex_data.low_nibble * 4);
		}
		else if (paddata[0].BTN_SQUARE)
		{
			if ((hex_data.data[hex_data.pos] & (0xF0 >> hex_data.low_nibble * 4)) == 0)
				hex_data.data[hex_data.pos] |= (0xF0 >> hex_data.low_nibble * 4);
			else
				hex_data.data[hex_data.pos] -= (0x10 >> hex_data.low_nibble * 4);
		}

		if ((hex_data.pos < hex_data.start) || (hex_data.pos >= hex_data.start + 0x120))
			hex_data.start = (hex_data.pos) & ~15;
	}

	Draw_HexEditor(&hex_data);
}

static int count_code_lines(const char * str)
{
	//Calc max
	int max = 0;

	for(max = 0; *str; ++str)
		max += (*str == '\n');

	if (max <= 0)
		max = 1;

	return max;
}

static void doPatchViewMenu(void)
{
	// Check the pads.
	if (readPad(0))
	{
		if (updatePadSelection(count_code_lines(selected_centry->codes)))
			(void)0;

		else if (paddata[0].BTN_CIRCLE)
		{
			SetMenu(last_menu_id[MENU_PATCH_VIEW]);
			return;
		}
	}
	
	Draw_CheatsMenu_View("Patch view");
}

static void doCodeOptionsMenu(void)
{
	code_entry_t* code = selected_centry;
	// Check the pads.
	if (readPad(0))
	{
		if(paddata[0].BTN_UP)
			move_selection_back(list_count(code->options[option_index].opts), 1);

		else if(paddata[0].BTN_DOWN)
			move_selection_fwd(list_count(code->options[option_index].opts), 1);

		else if (paddata[0].BTN_CIRCLE)
		{
			code->activated = 0;
			SetMenu(last_menu_id[MENU_CODE_OPTIONS]);
			return;
		}
		else if (paddata[0].BTN_CROSS)
		{
			code->options[option_index].sel = menu_sel;

			if (code->type == PATCH_COMMAND)
			{
				option_value_t* optval = list_get_item(code->options[option_index].opts, menu_sel);

				if (optval->value[0] == CMD_HEX_EDIT_FILE)
				{
					optval->value[1] = CMD_DECRYPT_FILE;
					execCodeCommand(code, optval->value+1);

					memset(&hex_data, 0, sizeof(hex_data));
					snprintf(hex_data.filepath, sizeof(hex_data.filepath), APOLLO_TMP_PATH "%s/%s", selected_entry->dir_name, optval->name);
					if (read_buffer(hex_data.filepath, &hex_data.data, &hex_data.size) < 0)
					{
						show_message("Unable to load\n%s", hex_data.filepath);
						SetMenu(last_menu_id[MENU_CODE_OPTIONS]);
						return;
					}

					SetMenu(MENU_HEX_EDITOR);
					return;
				}

				execCodeCommand(code, optval->value);
			}

			option_index++;
			
			if (option_index >= code->options_count)
			{
				SetMenu(last_menu_id[MENU_CODE_OPTIONS]);
				return;
			}
			else
				menu_sel = 0;
		}
	}
	
	Draw_CheatsMenu_Options();
}

static void doSaveDetailsMenu(void)
{
	// Check the pads.
	if (readPad(0))
	{
		if (updatePadSelection(count_code_lines(selected_centry->codes)))
			(void)0;

		if (paddata[0].BTN_CIRCLE)
		{
			if (selected_centry->name)
				free(selected_centry->name);
			if (selected_centry->codes)
				free(selected_centry->codes);
			free(selected_centry);

			SetMenu(last_menu_id[MENU_SAVE_DETAILS]);
			return;
		}
	}
	
	Draw_CheatsMenu_View(selected_entry->name);
}

static void doPatchMenu(void)
{
	// Check the pads.
	if (readPad(0))
	{
		if (updatePadSelection(list_count(selected_entry->codes)))
			(void)0;

		else if (paddata[0].BTN_CIRCLE)
		{
			SetMenu(last_menu_id[MENU_PATCHES]);
			return;
		}
		else if (paddata[0].BTN_CROSS)
		{
			selected_centry = list_get_item(selected_entry->codes, menu_sel);

			if (selected_centry->type != PATCH_NULL)
				selected_centry->activated = !selected_centry->activated;

			if (selected_centry->type == PATCH_COMMAND)
				execCodeCommand(selected_centry, selected_centry->codes);

			if (selected_centry->activated)
			{
				// Only activate Required codes if a cheat is selected
				if (selected_centry->type == PATCH_GAMEGENIE || selected_centry->type == PATCH_BSD)
				{
					code_entry_t* code;
					list_node_t* node;

					for (node = list_head(selected_entry->codes); (code = list_get(node)); node = list_next(node))
						if (code->flags & APOLLO_CODE_FLAG_REQUIRED && code->options_count == 0)
							code->activated = 1;
				}
				
				if (selected_centry->options)
				{
					option_index = 0;
					SetMenu(MENU_CODE_OPTIONS);
					return;
				}

				if (selected_centry->codes[0] == CMD_DELETE_SAVE)
				{
					selected_centry->activated = 0;
					SetMenu(last_menu_id[MENU_PATCHES]);
					return;
				}

				if (selected_centry->codes[0] == CMD_VIEW_RAW_PATCH)
				{
					selected_centry->activated = 0;
					selected_centry = LoadRawPatch();
					SetMenu(MENU_SAVE_DETAILS);
					return;
				}

				if (selected_centry->codes[0] == CMD_VIEW_DETAILS)
				{
					selected_centry->activated = 0;
					selected_centry = LoadSaveDetails(selected_entry);
					SetMenu(MENU_SAVE_DETAILS);
					return;
				}
			}
		}
		else if (paddata[0].BTN_TRIANGLE)
		{
			selected_centry = list_get_item(selected_entry->codes, menu_sel);

			if (selected_centry->type == PATCH_GAMEGENIE || selected_centry->type == PATCH_BSD ||
				selected_centry->type == PATCH_TROP_LOCK || selected_centry->type == PATCH_TROP_UNLOCK)
			{
				SetMenu(MENU_PATCH_VIEW);
				return;
			}
		}
	}
	
	Draw_CheatsMenu_Selection(menu_sel, 0xFFFFFFFF);
}

// Resets new frame
void drawScene(void)
{
	switch (menu_id)
	{
		case MENU_MAIN_SCREEN:
			doMainMenu();
			break;

		case MENU_TROPHIES: //Trophies Menu
			doSaveMenu(&trophies);
			break;

		case MENU_USB_SAVES: //USB Saves Menu
			doSaveMenu(&usb_saves);
			break;

		case MENU_HDD_SAVES: //HDD Saves Menu
			doSaveMenu(&hdd_saves);
			break;

		case MENU_ONLINE_DB: //Online Cheats Menu
			doSaveMenu(&online_saves);
			break;

		case MENU_CREDITS: //About Menu
			doAboutMenu();
			break;

		case MENU_SETTINGS: //Options Menu
			doOptionsMenu();
			break;

		case MENU_USER_BACKUP: //User Backup Menu
			doSaveMenu(&user_backup);
			break;

		case MENU_PATCHES: //Cheats Selection Menu
			doPatchMenu();
			break;

		case MENU_PATCH_VIEW: //Cheat View Menu
			doPatchViewMenu();
			break;

		case MENU_CODE_OPTIONS: //Cheat Option Menu
			doCodeOptionsMenu();
			break;

		case MENU_SAVE_DETAILS: //Save Details Menu
			doSaveDetailsMenu();
			break;

		case MENU_HEX_EDITOR: //Hex Editor Menu
			doHexEditor();
			break;

		case MENU_PS1VMC_SAVES: //PS1 VMC Menu
			doSaveMenu(&vmc1_saves);
			break;

		case MENU_PS2VMC_SAVES: //PS2 VMC Menu
			doSaveMenu(&vmc2_saves);
			break;
	}
}

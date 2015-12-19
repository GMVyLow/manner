// Copyright (c) Hercules Dev Team & hemagx, licensed under GNU GPL.
// See the LICENSE file

#include "common/hercules.h" /* Should always be the first Hercules file included! (if you don't make it first, you won't be able to use interfaces) */
#include "common/memmgr.h"
#include "common/mmo.h"
#include "common/strlib.h"
#include "map/clif.h"
#include "map/pc.h"
#include "map/npc.h"

#include "common/HPMDataCheck.h" /* should always be the last Hercules file included! (if you don't make it last, it'll intentionally break compile time) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

HPExport struct hplugin_info pinfo = {
	"Odin Manner",    // Plugin name
	SERVER_TYPE_MAP,// Which server types this plugin works with?
	"0.2",       // Plugin version
	HPM_VERSION, // HPM Version (don't change, macro is automatically updated)
};

struct manner_pcre {
	pcre *regex;
	pcre_extra *extra;
};

struct manner_config {
	int min_gm_level;
	int mute_after;
	int mute_time;
	char bypass_var[64];
};

struct player_manner {
	int32 mute_times;
	int32 changed; // when saving time we will compare this to mute_times and if it's not same we will save, i prefered to do this than add another lines to chat filter, it's already busy place.
};

struct manner_pcre *mannerlist = NULL;
struct manner_config config = {60, 0, 1, "chat_filter"};
unsigned int mannersize = 0;
unsigned int bypass_chat_filter = UINT_MAX - 1;


struct player_manner *manner_retrive_from_regs(struct map_session_data *sd)
{
	struct player_manner *data = getFromMSD(sd, 0);

	if( !data ) {
		CREATE(data, struct player_manner, 1);
		data->changed =	data->mute_times = pc_readglobalreg(sd, script->add_str("mute_filter"));
		addToMSD(sd, data, 0, true);
	}

	return data;
}

int pc_reg_received_posthook(int retVal, struct map_session_data *sd) 
{

	if(retVal == 0 || !sd) // sd missing case is impossible, but who knows... it's RAGNAROK!
		return 0;

	manner_retrive_from_regs(sd);
	return 1;
}

int chrif_save_prehook(struct map_session_data *sd, int *flag)
{
	struct player_manner *data;

	if ( sd && (data = getFromMSD(sd, 0) ) && data->changed != data->mute_times ) {
		pc_setglobalreg(sd, script->add_str("mute_filter"), data->mute_times);
		data->changed = data->mute_times;
	}

	return 1;
}

bool filter_chat(bool retVal, struct map_session_data *sd, int *format, char **name_, size_t *namelen_, char **message_, size_t *messagelen_) {
	char *message = *message_;
	unsigned int i;
	struct player_manner *data;
	char output[254]; // Compiler supposed to reuse the pre-allocated space but who knows, those few bytes doesn't worth lowering the speed.

	if( !retVal || !message )
		return false;

	if( !mannersize || !pc->can_talk(sd) )
		return retVal;

	if (pc_has_permission(sd, bypass_chat_filter) || (pc_get_group_level(sd) >= config.min_gm_level) || (pc_readglobalreg(sd, script->add_str(config.bypass_var))))
		return true;

	data = manner_retrive_from_regs(sd);

	for(i = 0; i < mannersize; ++i) {
		if(libpcre->exec(mannerlist[i].regex, mannerlist[i].extra, message, (int)strlen(message), 0, 0, NULL, 0) != PCRE_ERROR_NOMATCH) {
			sprintf(output, "Chat Filter: Yeah, uh, I don't think so, buddy...");
			clif->messagecolor_self(sd->fd, COLOR_RED, output);
			if ( config.mute_after > 0 ) {
				if ( data->mute_times >= config.mute_after ) {
					sc_start(NULL, &sd->bl, SC_NOCHAT, 100, MANNER_NOCHAT|MANNER_NOCOMMAND|MANNER_NOITEM|MANNER_NOROOM|MANNER_NOSKILL, config.mute_time * 60 * 1000);
					clif->GM_silence(sd, sd, 1);
					data->mute_times = 0;
				}else{
					++data->mute_times;
				}
			}
			return false;
		}
	}

	return true;
}

bool read_manner(const char* confpath) {
	FILE* fp;
	char line[1024], param[1024];
	const char *error = NULL;
	int offset;
	pcre *regex = NULL;
	pcre_extra *extra = NULL;

	fp = fopen(confpath, "r");
	if (fp == NULL) {
		ShowError("File not found: '%s'.\n", confpath);
		return false;
	}
	while(fgets(line, sizeof(line), fp)) {

		if((line[0] == '/' && line[1] == '/') || ( line[0] == '\0' || line[0] == '\n' || line[0] == '\r'))
			continue;

		if (sscanf(line, "%1023s", param) != 1)
			continue;

		regex = libpcre->compile(param, 0, &error, &offset, NULL);
		if ( regex == NULL ) {
			ShowError("Could not compile regex '%s'. Error: '%s'.\n", param, error);
			continue;
		}
		extra = libpcre->study(regex, 0, &error);
		if (error != NULL) {
			ShowError("Could not optimize '%s' Error: '%s'.\n", param, error);
			continue;
		}

		RECREATE(mannerlist, struct manner_pcre, mannersize + 1);
		mannerlist[mannersize].regex = regex;
		mannerlist[mannersize].extra = extra;
		++mannersize;
	}
	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", mannersize, confpath);
	return true;
}

bool read_manner_config(const char* confpath) {
	char line[1024], w1[1024], w2[1024];
	FILE* fp;

	fp = fopen(confpath, "r");

	if (fp == NULL) {
		ShowError("File not found: '%s' \n", confpath);
		return false;
	}

	while(fgets(line, sizeof(line), fp))
	{
		if(line[0] == '/' && line[1] == '/')
			continue;

		if (sscanf(line, "%[^:]: %[^\r\n]", w1, w2) != 2)
			continue;

		if(!strcmpi(w1, "gm_min_level"))
			config.min_gm_level = atoi(w2);
		else if (!strcmpi(w1, "mute_after"))
			config.mute_after = atoi(w2);
		else if (!strcmpi(w1, "mute_time"))
			config.mute_time = atoi(w2);
		else if (!strcmpi(w1, "bypass_var"))
			strncpy(config.bypass_var, w2, sizeof(config.bypass_var));
	}

	ShowStatus("Chat filter configuration has been loaded.\n");
	return true;
}

void clean_manner(void) {
	unsigned int i;

	for (i = 0; i < mannersize; i++) {

		libpcre->free(mannerlist[i].regex);

		if (mannerlist[i].extra)
			libpcre->free(mannerlist[i].extra);
	}
	aFree(mannerlist);
	mannerlist = NULL;
	mannersize = 0;
}

ACMD(reloadmanner) {
	
	clean_manner();
	read_manner("conf/plugin/manner.txt");
	read_manner_config("conf/plugin/manner.conf");
	return true;
}

HPExport void plugin_init(void) {
	addGroupPermission("bypass_chat_filter", bypass_chat_filter);
	addHookPost("clif->process_message", filter_chat);
	addHookPost("pc->reg_received", pc_reg_received_posthook);
	addHookPre("chrif->save", chrif_save_prehook);
	addAtcommand("reloadmanner", reloadmanner);
	read_manner("conf/plugin/manner.txt");
	read_manner_config("conf/plugin/manner.conf");
}
HPExport void plugin_final(void) {
	clean_manner();
}
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

struct manner_pcre *mannerlist = NULL;
unsigned int mannersize = 0;
unsigned int bypass_chat_filter = UINT_MAX - 1;


bool filter_chat(bool retVal, struct map_session_data *sd, int *format, char **name_, size_t *namelen_, char **message_, size_t *messagelen_) {
	char *message = *message_;
	unsigned int i;
	char output[254]; // Compiler supposed to reuse the pre-allocated space but who knows, those few bytes doesn't worth lowering the speed.

	if( !retVal || !message )
		return false;

	if( !mannersize || pc_has_permission(sd, bypass_chat_filter) || !pc->can_talk(sd) )
		return retVal;

	for(i = 0; i < mannersize; ++i) {
		if(libpcre->exec(mannerlist[i].regex, mannerlist[i].extra, message, (int)strlen(message), 0, 0, NULL, 0) != PCRE_ERROR_NOMATCH) {
			sprintf(output, "Chat Filter: Yeah, uh, I don't think so, buddy...");
			clif->messagecolor_self(sd->fd, COLOR_RED, output);
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
	return true;
}

HPExport void plugin_init(void) {
	addGroupPermission("bypass_chat_filter", bypass_chat_filter);
	addHookPost("clif->process_message", filter_chat);
	addAtcommand("reloadmanner", reloadmanner);
	read_manner("conf/plugin/manner.txt");
}
HPExport void plugin_final(void) {
	clean_manner();
}
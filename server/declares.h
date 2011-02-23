// Please see LICENSE file.
#ifndef DECLARES_H
#define DECLARES_H

#include <string>
#include "../common/col_codes.h"

enum e_GameMode {
	GM_DOM=0, // dominion
	GM_CONQ, // conquest
	GM_STEAL, // steal
	GM_DESTR // destroy
};

enum e_MapType {
	MT_DUNGEON=0,
	MT_OUTDOOR,
	MAX_MAPTYPE
};
const std::string short_mtype_name[MAX_MAPTYPE] = { "dun", "out" };

const unsigned char team_colour[3] = { C_NEUT_FLAG, C_GREEN_PC, C_PURPLE_PC };

const unsigned char DEF_MSG_COL = 7;

#endif

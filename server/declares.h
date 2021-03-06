// Please see LICENSE file.
#ifndef DECLARES_H
#define DECLARES_H

#include <string>
#include "../common/col_codes.h"

enum e_GameMode {
	GM_DOM=0, // dominion
	GM_CONQ, // conquest
	GM_STEAL, // steal
	GM_DESTR, // destroy
	GM_TDM // team deathmatch
};

enum e_MapType {
	MT_DUNGEON=0,
	MT_OUTDOOR,
	MT_COMPLEX,
	MAX_MAPTYPE
};
const std::string short_mtype_name[MAX_MAPTYPE] = { "dun", "out", "com" };

const std::string long_sector_name[] = {
	"north", "north-east", "east", "south-east", "south", "south-west",
	"west", "north-west", "central" };

const unsigned char DEF_MSG_COL = 15;

#endif

// Please see LICENSE file.
#ifndef DECLARES_H
#define DECLARES_H

#include "../common/col_codes.h"

enum e_GameMode {
	GM_DOM=0, // dominion
	GM_CONQ, // conquest
	GM_STEAL, // steal
	GM_DESTR // destroy
};

const unsigned char team_colour[3] = { C_NEUT_FLAG, C_GREEN_PC, C_PURPLE_PC };

const unsigned char DEF_MSG_COL = 7;

#endif

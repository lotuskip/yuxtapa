// Please see LICENSE file.
#ifndef CMDS_H
#define CMDS_H

#include "players.h"

// Returns true if the command should be shown to all players.
bool process_cmd(const std::list<Player>::iterator pit, std::string &cmd);

void shuffle_teams();
bool drop_a_bot();
short num_bots();

#endif

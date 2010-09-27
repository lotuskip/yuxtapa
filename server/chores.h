// Please see LICENSE file.
#ifndef CHORES_H
#define CHORES_H

#include "players.h"
#include "../common/coords.h"
#include <set>


void process_action(const Axn &axn, const std::list<Player>::iterator pit);
void process_swaps();
void progress_chore(const std::list<Player>::iterator pit);
void trap_detection(const std::list<Player>::iterator pit);

// Kills a PC, removes owned entities, reassigns viewpoints;
// assumes that 'pit' is an alive player!
void kill_player(const std::list<Player>::iterator pit);
// Sends a message about player dying and calls kill_player.
void player_death(const std::list<Player>::iterator pit, const std::string &way,
	const bool corpse);
// Use player_death when a PC dies and the death should be "recorded".
// Use only kill_player when "internally" removing a player without
// recording a death.

class OwnedEnt;
// Handles missile (arrow/mm/zap) collisions with anything; returns
// true if a collision happens at 'c'.
bool missile_coll(OwnedEnt* mis, const Coords &c);

#endif

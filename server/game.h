// Please see LICENSE file.
#ifndef SERVER_GAME_H
#define SERVER_GAME_H

#include "../common/netutils.h"
#include "players.h"
#include "../common/col_codes.h"

class Map;

const std::string str_team[2] = { "Green", "Purple" };
const unsigned char team_colour[3] = { C_NEUT_FLAG, C_GREEN_PC, C_PURPLE_PC };

// describes "one game", playing of a single map
namespace Game
{
	void next_map(); // called when starting intermission
	void init_game(); // processes next step in initialisation, or does nothing if ready
	void start(); // game starts now, reset clock
	bool process_turn(); /* most of the game logic happens here, returns
		true if the map should end */
	
	void player_action(std::list<Player>::iterator pit, const Axn action);

	// Construct msg containing information about teams&players. If 'to'
	// is cur_players.end(), will broadcast to all players. Otherwise
	// sends to 'to'.
	void construct_team_msgs(const std::list<Player>::const_iterator to);
	// send team update:
	void send_team_upds(const std::list<Player>::const_iterator to);	
	// send clocksyncs:
	void send_times(const std::list<Player>::const_iterator to);
	// send new PC info (no broadcasting!)
	void send_state_change(const std::list<Player>::const_iterator to);

	void class_switch(const std::list<Player>::iterator pit,
		const e_Class newcl);
	void team_switch(const std::list<Player>::iterator pit);
	std::list<Player>::iterator remove_player(std::list<Player>::iterator pit,
		const std::string &reason);

	SerialBuffer &get_miniview();

	extern unsigned short curturn;
	extern Map *curmap;
}

#endif

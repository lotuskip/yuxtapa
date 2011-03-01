// Please see LICENSE file.
#ifndef SERVER_GAME_H
#define SERVER_GAME_H

#include "../common/netutils.h"
#include "players.h"

class Map;

const std::string str_team[2] = { "Green", "Purple" };

// describes "one game", playing of a single map
namespace Game
{
	void next_map(const std::string &loadmap); // called when starting intermission
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
	// send team flag info:
	void send_flag_upds(const std::list<Player>::const_iterator to);
	// send clocksyncs:
	void send_times(const std::list<Player>::const_iterator to);
	// send new PC info (no broadcasting!)
	void send_state_change(const std::list<Player>::const_iterator to);

	void class_switch(const std::list<Player>::iterator pit,
		const e_Class newcl);
	void team_switch(const std::list<Player>::iterator pit);
	std::list<Player>::iterator remove_player(std::list<Player>::iterator pit,
		const std::string &reason);

	// Remove all entities owned by the given player
	void del_owneds(const std::list<Player>::const_iterator pit);

	SerialBuffer &get_miniview();

	extern unsigned short curturn;
	extern Map *curmap;
}

#endif

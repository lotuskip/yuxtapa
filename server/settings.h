// Please see LICENSE file.
#ifndef SERVER_SETTINGS_H
#define SERVER_SETTINGS_H

#include <string>
#ifndef MAPTEST
#include "declares.h"
#endif

namespace Config
{
#ifndef MAPTEST
	// teambalance settings
	enum { TB_OFF=0, TB_PASSIVE, TB_ACTIVE };

	// all those settings that are just integers are tabled
	enum {
		IS_MAXPLAYERS=0,
		IS_TEAMBALANCE, // actually an enum
		IS_MAPSIZE,
		IS_MAPSIZEVAR,
		IS_CLASSLIMIT,
		IS_TURNMS,
		IS_INTERM_SECS,
		IS_PORT,
		IS_STATPURGE,
		MAX_INT_SETTING	};

	void read_config();

	std::string &get_config_dir();
	std::string &greeting_str();
	void set_greeting_str(const std::string &s);

	std::string &get_botexe();

	e_GameMode next_game_mode();
	std::string game_modes_str();
	e_MapType next_map_type();
	std::string map_types_str();

	const std::string &new_bot_name();

	extern unsigned int int_settings[MAX_INT_SETTING];
#endif
	std::string &get_mapdir();
}

#endif

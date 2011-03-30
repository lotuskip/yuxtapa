// Please see LICENSE file.
#ifndef SERVER_SETTINGS_H
#define SERVER_SETTINGS_H

#include <string>
#ifndef MAPTEST
#include "declares.h"

enum { CMD_SERVERINFO=0, CMD_ADMINLVL, CMD_TEAMS, CMD_SHUFFLE, CMD_STOREMAP,
	CMD_LISTMAPS, CMD_NEXTMAP, CMD_TEAMBAL, CMD_KICK, CMD_MUTE, CMD_UNMUTE,
	CMD_SETAL, CMD_BAN, CMD_SERVERMSG, CMD_CLEARBANS, CMD_SPAWNBOTS,
	CMD_DROPBOTS, CMD_LOG, CMD_PLINFO, CMD_PUTTEAM, CMD_SETCLASS, CMD_HOWFAIR,
	CMD_LSCMD, MAX_SERVER_CMD };

const std::string server_cmd[MAX_SERVER_CMD] = { "serverinfo", "adminlvl",
	"teams", "shuffle", "storemap", "listmaps", "nextmap", "teambal", "kick",
	"mute", "unmute", "setal", "ban", "servermsg", "clearbans", "spawnbots",
	"dropbots", "log", "plinfo", "putteam", "setclass", "howfair", "lscmd" };

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
	bool is_cmd_banned(const char index);

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

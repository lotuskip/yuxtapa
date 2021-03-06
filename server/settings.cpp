// Please see LICENSE file.
#include "settings.h"
#ifndef MAPTEST
#include "log.h"
#include "../common/confdir.h"
#include "../common/utfstr.h"
#include "../common/constants.h"
#include "../common/classes_common.h"
#include "../common/util.h"
#include <fstream>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
using namespace std;

const unsigned char MAX_LIST_LEN = 80;

const string short_mode_name[] = { "dom", "con", "ste", "des", "tdm" };

// default settings:
const unsigned int default_sets[Config::MAX_INT_SETTING] = {
25, // max players
Config::TB_PASSIVE,
100, // map size
20, // variance
0, // class limit
250, // turn in ms
45, // intermission in seconds
12360, // port
0, // ip (4: IPv4, 6: IPv6, 0: unspecified)
24, // stat purge (h)
1, // safe chasms (how many warnings are given)
0 // min players
};

// keys to these:
const string keywords[Config::MAX_INT_SETTING] = {
"maxplayers", "teambalance", "mapsize", "mapsizevar", "classlimit",
"turnms", "interm", "port", "ipv", "statpurge", "safe_chasms", "minplayers" };

// hard limits (just staying within these is not sufficient alone):
const unsigned int hard_lims[Config::MAX_INT_SETTING][2] = {
{ 2, 200 }, // max players
{ Config::TB_OFF, Config::TB_ACTIVE },
{ 70, 365 }, /* map size; applying the below maximum variance gives
final map size span of 42--511 */
{ 0, 40 }, // variance
{ 0, 200 }, // classlimit; no real upper limit since 0 means "no limit"
{ 50, 2000 }, // turn ms
{ 10, 120 }, // intermission secs
{ 1024, 61000 }, // port
{ 0, 6 }, // port
{ 0, 50000 }, // stat purge; again 0 means "no limit"
{ 0, 255 }, // safe chasms (number of warnings given)
{ 0, 200 } // min players (must also be <= maxplayers)
};

string confdir_str = "";
string greeting = "";
string botexe = "";
string banned_cmds = "";
#endif // not maptest build
std::string mapdir = "";
#ifndef MAPTEST
vector<char> poss_modes; // e_GameMode
vector<char> poss_mtypes; // e_MapType

vector<string> botnames;
vector<string>::const_iterator botname_iter;


bool check_path_setting(const string &name, string &val, const bool x)
{
	if(x)
	{
		if(access(val.c_str(), X_OK) == -1)
		{
			to_log("Config error: the \'" + name + "\' setting does not "
				"point to a valid executable file: \"" + val + "\".");
			val.clear();
			return true;
		}
		return false;
	}
	struct stat buf;
	if(stat(val.c_str(), &buf) || !(S_ISDIR(buf.st_mode)))
	{
		to_log("Config error: the \'" + name + "\' setting does not "
			"point to a valid directory: \"" + val + "\".");
		val.clear();
		return true;
	}
	return false;
}

string construct_value_list(const vector<char> &l, string const* const s)
{
	string ret = "";
	for(vector<char>::const_iterator it = l.begin(); it != l.end(); ++it)
		ret += s[*it] + '/';
	ret.erase(ret.size()-1, 1);
	if(ret.size() > MAX_LIST_LEN)
	{
		ret.erase(ret.rfind('/', MAX_LIST_LEN-4));
		ret += "...";
	}
	return ret;
}

bool validate_int_set(const int i)
{
	using namespace Config;
	if(i == IS_IPV)
		return (int_settings[IS_IPV] != 0 && int_settings[IS_IPV] != 4
			&& int_settings[IS_IPV] != 6);
	return (int_settings[i] < hard_lims[i][0] || int_settings[i] > hard_lims[i][1]);
}

bool validate_classlim()
{
	using namespace Config;
	if(int_settings[IS_CLASSLIMIT]
		&& int_settings[IS_CLASSLIMIT]*2*NO_CLASS < int_settings[IS_MAXPLAYERS])
	{
		int_settings[IS_CLASSLIMIT] = 0;
		return true;
	}
	return false;
}

bool validate_minp()
{
	using namespace Config;
	if(int_settings[IS_MINPLAYERS] > int_settings[IS_MAXPLAYERS]
		|| (int_settings[IS_MINPLAYERS] && !botexe.length()))
	{
		int_settings[IS_MINPLAYERS] = 0;
		return true;
	}
	return false;
}

} // end of local namespace


unsigned int Config::int_settings[Config::MAX_INT_SETTING];

bool Config::parse_config(const string &s, const bool from_conf)
{
	string keyw, olds;
	vector<char> old_chv;
	vector<string> old_sv;
	int oldi, i;
	stringstream ss(s, ios_base::in);

	ss >> keyw;
	// first check if it is a keyword:
	for(i = 0; i < MAX_INT_SETTING; ++i)
	{
		if(keyw == keywords[i])
		{
			oldi = int_settings[i];
			// teambalance separately:
			if(i == IS_TEAMBALANCE)
			{
				ss >> keyw;
				if(keyw == "active")
					int_settings[i] = TB_ACTIVE;
				else if(keyw == "passive")
					int_settings[i] = TB_PASSIVE;
				else if(keyw == "off")
					int_settings[i] = TB_OFF;
				else if(from_conf)
					to_log("Warning: unknown team balance setting \'"
						+ keyw + "\' in config.");
				else return true;
			}
			else
				ss >> int_settings[i];
			break;
		}
	}
	if(i < MAX_INT_SETTING) // found
	{
		if(!from_conf) // validate now
		{
			if(validate_int_set(i))
			{
				int_settings[i] = oldi;
				return true;
			}
			validate_classlim();
			validate_minp();
		}
		return false;
	}
	// check non-numeric settings:
	if(keyw == "greeting")
	{
		greeting = ss.str();
		greeting.erase(0, greeting.find(' '));
		if(!greeting.empty())
		{
			greeting.erase(0, 1);
			if(greeting.size() >= MAXIMUM_STR_LEN)
			{
				if(from_conf)
					to_log("The greeting string in the config is too long!");
				while(greeting.size() >= MAXIMUM_STR_LEN)
					del(greeting, 0);
				return false;
			}
			// If the greeting contains words longer than MSG_WIN_X,
			// they must be splitted:
			size_t ind, jnd = 0;
			while((ind = greeting.find(' ', jnd)) != string::npos)
			{
				if(ind - jnd > MSG_WIN_X)
				{
					greeting.insert(jnd + MSG_WIN_X, 1, ' ');
					++ind;
				}
				if(ind == greeting.size()-1)
					break;
				jnd = ind+1;
			}
		}
		return false;
	}
	if(keyw == "bots")
	{
		olds = botexe;
		ss >> botexe;
		if(check_path_setting(keyw, botexe, true) && !from_conf)
		{
			botexe = olds;
			return true;
		}
		return false;
	}
	if(keyw == "botnames")
	{
		size_t ind;
		old_sv = botnames;
		botnames.clear();
		while(ss >> keyw)
		{
			if(num_syms(keyw) > MAX_NICK_LEN)
			{
				if(from_conf)
					to_log("Warning: bot name '" + keyw + "' is too long.");
				del_syms(keyw, MAX_NICK_LEN);
			}
			ind = 0;
			while((ind = keyw.find('_', ind)) != string::npos)
				keyw.replace(ind, 1, 1, ' ');
			botnames.push_back(keyw);
		}
		if(!from_conf && botnames.empty())
			botnames = old_sv;
		return false;
	}
	if(keyw == "mapdir")
	{
		olds = mapdir;
		ss >> mapdir;
		if(check_path_setting(keyw, mapdir, false) && !from_conf)
		{
			mapdir = olds;
			return true;
		}
		// Make sure nonempty value ends in '/':
		if(!mapdir.empty() && mapdir[mapdir.size()-1] != '/')
			mapdir += '/';
		return false;
	}
	if(keyw == "mode")
	{
		old_chv = poss_modes;
		poss_modes.clear();
		while(ss >> keyw)
		{
			if(keyw == "dominion")
				poss_modes.push_back(GM_DOM);
			else if(keyw == "conquest")
				poss_modes.push_back(GM_CONQ);
			else if(keyw == "steal")
				poss_modes.push_back(GM_STEAL);
			else if(keyw == "destroy")
				poss_modes.push_back(GM_DESTR);
			else if(keyw == "team-dm")
				poss_modes.push_back(GM_TDM);
			else if(from_conf)
				to_log("Unrecognised game mode string \'" + keyw + "\'!");
		}
		if(!from_conf && poss_modes.empty())
		{
			poss_modes = old_chv;
			return true;
		}
		return false;
	}
	if(keyw == "maptype")
	{
		old_chv = poss_mtypes;
		poss_mtypes.clear();
		while(ss >> keyw)
		{
			if(keyw == "dungeon")
				poss_mtypes.push_back(MT_DUNGEON);
			else if(keyw == "outdoor")
				poss_mtypes.push_back(MT_OUTDOOR);
			else if(keyw == "complex")
				poss_mtypes.push_back(MT_COMPLEX);
			else if(from_conf)
				to_log("Unrecognised map type string \'" + keyw + "\'!");
		}
		if(!from_conf && poss_mtypes.empty())
		{
			poss_mtypes = old_chv;
			return true;
		}
		return false;
	}
	if(keyw == "ban_command")
	{
		while(ss >> keyw)
		{
			for(i = 0; i < MAX_SERVER_CMD; ++i)
			{
				if(keyw == server_cmd[i] && banned_cmds.find(char(i)) == string::npos)
				{
					banned_cmds += char(i);
					break;
				}
			}
			if(i == MAX_SERVER_CMD && from_conf)
				to_log("Unrecognised banned command \'" + keyw + "\' in config!");
		}
		return false;
	}
	return true;
}

void Config::read_config()
{
	// first, initialize with defaults:
	int i;
	for(i = 0; i < MAX_INT_SETTING; ++i)
		int_settings[i] = default_sets[i];

	string s = get_config_dir() + "server.conf";
	ifstream file(s.c_str());
	if(!file)
		to_log("Warning: config file does not exist or is not readable!");
	while(file)
	{
		// This is very crude parsing; if the file is not right it probably is read all wrong
		getline(file, s);
		if(s.empty() || s[0] == '#')
			continue;
		if(parse_config(s, true))
		{
			to_log("Warning: could not parse the following line in config file:");
			to_log("\t\"" + s + "\"");
		}
	}
	
	// everything extracted (or failed altogether), check:
	for(i = 0; i < MAX_INT_SETTING; ++i)
		if(validate_int_set(i))
		{
			int_settings[i] = default_sets[i];
			to_log("Warning: config gave invalid value for \'" + keywords[i] + "\'.");
		}
	if(validate_classlim())
		to_log("Config error: classlimit is too small "
			"with respect to the number of players allowed!");
	if(validate_minp())
		to_log("Config error: minplayers forced to 0!");
	// Check modes:
	if(poss_modes.empty())
	{
		poss_modes.push_back(GM_DOM);
		poss_modes.push_back(GM_CONQ);
		poss_modes.push_back(GM_STEAL);
		poss_modes.push_back(GM_DESTR);
		poss_modes.push_back(GM_TDM);
	}
	// Similarly check map types:
	if(poss_mtypes.empty())
	{
		poss_mtypes.push_back(MT_DUNGEON);
		poss_mtypes.push_back(MT_OUTDOOR);
		poss_mtypes.push_back(MT_COMPLEX);
	}
	// Force at least one bot name:
	if(botnames.empty())
		botnames.push_back("Mr. Brown");
	botname_iter = botnames.begin();
}


string &Config::get_config_dir()
{
	if(confdir_str.empty())
		confdir_str = get_conf_dir();
	return confdir_str;
}

string &Config::greeting_str() { return greeting; }

string &Config::get_botexe() { return botexe; }

e_GameMode Config::next_game_mode()
{
	return e_GameMode(poss_modes[randor0(poss_modes.size())]);
}

string Config::game_modes_str()
{
	return construct_value_list(poss_modes, short_mode_name);
}

e_MapType Config::next_map_type()
{
	return e_MapType(poss_mtypes[randor0(poss_mtypes.size())]);
}

string Config::map_types_str()
{
	return construct_value_list(poss_mtypes, short_mtype_name);
}

const string &Config::new_bot_name()
{
	if(++botname_iter == botnames.end())
		botname_iter = botnames.begin();
	return *botname_iter;
}

bool Config::is_cmd_banned(const char index)
{
	return banned_cmds.find(index) != string::npos;
}

#endif // not maptest build

std::string &Config::get_mapdir() { return mapdir; }


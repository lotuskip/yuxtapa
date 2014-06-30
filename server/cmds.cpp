// Please see LICENSE file.
#ifndef MAPTEST
#include "cmds.h"
#include "server.h"
#include "log.h"
#include "game.h"
#include "network.h"
#include "settings.h"
#include "chores.h"
#include "viewpoint.h"
#include "../common/util.h"
#include <cctype>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <netdb.h>
#include <dirent.h>
#include <algorithm>
#include <cstring>
#ifdef DEBUG
#include <iostream>
#endif

extern bool intermission;
extern unsigned char num_players[2]; // defined in game.cpp

namespace
{
using namespace std;

const unsigned char cmd_respond_msg_col = 12;
const unsigned short KILLS_DEATHS_REQ_FOR_COMP = 20;

const string ad_lvl_name[4] = { "guest", "regular", "trusted user",
	"admin" };
const string team_balance_str[] = { "no", "passive", "active" };


// Count total kills, reduce team kills, return always >= 0
unsigned long total_kills(const list<PlayerStats>::iterator st)
{
	unsigned long res = 0;
	for(char i = 0; i < MAX_WAY_TO_KILL; ++i)
		res += st->kills[i];
	if(res > st->tks)
		res -= st->tks;
	return res;
}

// For sorting players by their "level" for team shuffling.
// We want a *descending* order; return true if *i is better than *j.
// Note that this ordering is completely hidden from the players!
bool pl_level_cmp(const list<Player>::const_iterator i, const list<Player>::const_iterator j)
{
	// Ordering is based on a consideration of kills/deaths & playing time:
	unsigned long resi = 0, resj = 0;
	for(char ch = 0; ch < NO_CLASS; ++ch)
	{
		resi += i->stats_i->time_played[ch];
		resj += j->stats_i->time_played[ch];
	}
	resi /= NO_CLASS; // averages of times played as each class
	resj /= NO_CLASS;
	// only take kill/death ratio into account if have enough deaths:
	if(i->stats_i->deaths >= KILLS_DEATHS_REQ_FOR_COMP)
		resi *= float(total_kills(i->stats_i))/i->stats_i->deaths;
	if(j->stats_i->deaths >= KILLS_DEATHS_REQ_FOR_COMP)
		resj *= float(total_kills(j->stats_i))/j->stats_i->deaths;
	// It might be that resi == resj, although this is very unlikely.
	// In that case, less healing received wins (or arbitrarily j if equal):
	if(resi != resj)
		return (resi > resj);
	return (i->stats_i->healing_recvd > j->stats_i->healing_recvd);
}

// If 's' can be uniquely matched to a single player's nick, returns
// that player.
list<Player>::iterator id_pl_by_nick_part(const string &s)
{
	list<Player>::iterator foundit = cur_players.end();
	bool found_several = false;
	for(list<Player>::iterator pit = cur_players.begin();
		pit != cur_players.end(); ++pit)
	{
		if(pit->nick == s) // exact match overrules any partial
			return pit;
		// else check for a partial match:
		if(pit->nick.find(s) != string::npos)
		{
			if(foundit != cur_players.end()) // already found one!
				found_several = true;
			foundit = pit;
		}
	}
	if(found_several) // could not match uniquely
		return cur_players.end();
	return foundit;
}


bool next_bot(list<Player>::iterator &it)
{
	while(it != cur_players.end() && it->botpid == -1)
		++it;
	return (it != cur_players.end());
}

} // end local namespace


bool process_cmd(const list<Player>::iterator pit, string &cmd)
{
	cmd.erase(0, 1); // remove '!'
	size_t i = cmd.find(' ');
	int tmpi;
	string keyw = cmd.substr(0, i);
	transform(keyw.begin(), keyw.end(), keyw.begin(), (int(*)(int))tolower);

	char ind;
	for(ind = 0; ind < MAX_SERVER_CMD; ++ind)
	{
		if(keyw == server_cmd[ind])
			break;
	}
	if(ind == MAX_SERVER_CMD) // unrecognized
		return false;
	if(Config::is_cmd_banned(ind))
	{
		if(!pit->muted)
			Network::to_chat("Command \'!" + keyw + "\' disabled on this server.");
		return false;
	}

	switch(ind)
	{
	case CMD_SERVERINFO: // req: not muted
		if(!pit->muted)
		{
			using namespace Config;
			keyw = "Max players: "
				+ lex_cast(int_settings[IS_MAXPLAYERS])
				+ ", mapsize ";
			short minmsize = (100 - int_settings[IS_MAPSIZEVAR])
				*int_settings[IS_MAPSIZE]/100;
			short maxmsize = (100 + int_settings[IS_MAPSIZEVAR])
				*int_settings[IS_MAPSIZE]/100;
			keyw += lex_cast(minmsize) + '-'
				+ lex_cast(maxmsize) + ", classlim ";
			keyw += lex_cast(int_settings[IS_CLASSLIMIT])
				+ ", turn " + lex_cast(int_settings[IS_TURNMS]);
			keyw += "ms, intermission "
				+ lex_cast(int_settings[IS_INTERM_SECS]);
			keyw += "s, statpurge "
				+ lex_cast(int_settings[IS_STATPURGE]) + "h, "
				+ team_balance_str[int_settings[IS_TEAMBALANCE]];
			keyw += " teambalance, modes: " + game_modes_str();
			keyw += ", map types: " + map_types_str() + '.';
			Network::to_chat(keyw);
		}
		return false; // may broadcast
	case CMD_ADMINLVL: // req: none
		if(i != string::npos && i < cmd.size()-1)
		{
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1));
			if(nit != cur_players.end())
			{
				keyw = nit->nick + " has admin level "
					+ lex_cast(nit->stats_i->ad_lvl)
					+ admin_lvl_str[nit->stats_i->ad_lvl];
				Network::construct_msg(keyw, cmd_respond_msg_col); 
				Network::send_to_player(*pit);
			}
		}
		return true; // a "private" request
	case CMD_TEAMS: // req: none
		Game::construct_team_msgs(pit);
		return true; // a "private" request
	case CMD_SHUFFLE: //req: AL >= 2 && not muted
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_TU)
			shuffle_teams();
		return false; // may broadcast
	case CMD_STOREMAP: // req: AL >= 3 && not muted
		if(!intermission && !pit->muted && pit->stats_i->ad_lvl >= AL_ADMIN
			&& !Config::get_mapdir().empty()
			&& i != string::npos && i < cmd.size()-1)
		{
			if(Game::curmap->save_to_file(cmd.substr(i+1)))
				keyw = "Current map saved as \'" + cmd.substr(i+1) + "\'.";
			else
				keyw = "Could not save the map!";
			Network::to_chat(keyw);
		}
		return false; // may broadcast
	case CMD_LISTMAPS: // req: AL >= 2 && not muted
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_TU)
		{
			DIR *dp;
			if((dp = opendir(Config::get_mapdir().c_str())) != NULL)
			{
				keyw = "Stored maps:";
    			struct dirent *dirp;
				while((dirp = readdir(dp)) != NULL)
				{
					if(strcmp(dirp->d_name, ".") && strcmp(dirp->d_name, ".."))
					{
						keyw += ' ';
						keyw += dirp->d_name;
						keyw += ',';
					}
				}
				if(keyw[keyw.size()-1] == ',')
					keyw.erase(keyw.size()-1,1);
				else
					keyw += " [none]";
				// If there's a crazy amount, the resulting string might be too long!
				if(keyw.size() > MAXIMUM_STR_LEN)
					keyw.replace(keyw.rfind(' ', MAXIMUM_STR_LEN-4),
						keyw.size(), "...");
				Network::to_chat(keyw);
				closedir(dp);
			}
		}
		return false; // may broadcast
	case CMD_NEXTMAP: // req: AL >= 2 && not muted
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_TU)
		{
			// Check if requested a map by name:
			if(i != string::npos && i < cmd.size()-1)
				keyw = cmd.substr(i+1);
			else
				keyw.clear();
			next_map_forced(keyw);
		}
		return false; // may broadcast
	case CMD_KICK: // req: AL > max(1, target's) && not muted
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_TU && i != string::npos
			&& i < cmd.size()-1)
		{
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1));
			if(nit != cur_players.end() &&
				pit->stats_i->ad_lvl > nit->stats_i->ad_lvl)
			{
				keyw = "You have been kicked from this server.";
				Network::construct_msg(keyw, DEF_MSG_COL);
				Network::send_to_player(*nit);
				Game::remove_player(nit, " kicked by " + pit->nick + '.');
			}
		}
		return false; // may broadcast
	case CMD_MUTE: // req: AL > target's && not muted
		if(!pit->muted && i != string::npos && i < cmd.size()-1)
		{
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1));
			if(nit != cur_players.end() && !nit->muted &&
				pit->stats_i->ad_lvl > nit->stats_i->ad_lvl)
			{
				nit->muted = true;
				keyw = "You have been muted by " + pit->nick + '.';
				Network::construct_msg(keyw, cmd_respond_msg_col);
				Network::send_to_player(*nit);
			}
		}
		return false; // may broadcast
	case CMD_UNMUTE: // req: AL > target's && not muted
		if(!pit->muted && i != string::npos && i < cmd.size()-1)
		{
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1));
			if(nit != cur_players.end() && nit->muted &&
				pit->stats_i->ad_lvl > nit->stats_i->ad_lvl)
			{
				nit->muted = false;
				keyw = "You were unmuted by " + pit->nick + '.';
				Network::construct_msg(keyw, cmd_respond_msg_col);
				Network::send_to_player(*nit);
			}
		}
		return false; // may broadcast
	case CMD_SETAL:
		// there has to be room for !setal [nick] X, where X is a digit
		if(!pit->muted && i != string::npos && i < cmd.size()-3
			&& cmd[cmd.size()-1] <= '4' && cmd[cmd.size()-1] >= '0')
		{
			keyw = cmd.substr(i+1, cmd.size()-i-3);
			list<Player>::iterator nit = id_pl_by_nick_part(keyw);
			if(nit != cur_players.end())
			{
				e_AdminLvl al = e_AdminLvl(cmd[cmd.size()-1] - '0');
				if(al == AL_SADMIN) // superadmin
				{
					// There must be only one player in the server's knowledge:
					if(known_players.size() == 1)
					{
						known_players.front().ad_lvl = al;
						keyw = "You are now the Super-Admin of this server. Your player ID is "
							+ lex_cast(known_players.front().ID) + '.';
						Network::construct_msg(keyw, cmd_respond_msg_col);
						Network::send_to_player(*pit);
						return true; // no broadcast
					}
				}
				else if(al <= pit->stats_i->ad_lvl
					&& pit->stats_i->ad_lvl > nit->stats_i->ad_lvl)
				{
					nit->stats_i->ad_lvl = al;
					keyw = "Your AL was set to *" + ad_lvl_name[al]
						+ "* by " + pit->nick + '!';
					Network::construct_msg(keyw, cmd_respond_msg_col);
					Network::send_to_player(*nit);
				}
			} // player identified
		}
		return false; // may broadcast
	case CMD_BAN: // req: AL > max(2, target's) && not muted
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_ADMIN
			&& i != string::npos && i < cmd.size()-1)
		{
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1));
			if(nit != cur_players.end() &&
				pit->stats_i->ad_lvl > nit->stats_i->ad_lvl)
			{
				keyw = "You have been banned from this server.";
				Network::construct_msg(keyw, DEF_MSG_COL);
				Network::send_to_player(*nit);
				banlist().push_back(nit->address);
				Game::remove_player(nit, " banned by " + pit->nick + '.');
			}
		}
		return false; // may broadcast
	case CMD_CLEARBANS: // req: AL >= 3 && not muted
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_ADMIN)
			banlist().clear();
		return false; // may broadcast
	case CMD_SPAWNBOTS: // req: AL >= 3 && not muted
		if(!Config::get_botexe().empty() && pit->stats_i->ad_lvl >= AL_ADMIN && !pit->muted
			&& i != string::npos && i < cmd.size()-1 && isdigit(cmd[i+1]))
		{
			using namespace Config;
			// Spawn as many as requested, as long as that won't make the
			// server over-full:
			char num = min(int(cmd[i+1]-'0'),
				int(int_settings[IS_MAXPLAYERS] - cur_players.size()));
			pid_t pid;
			for(; num > 0; --num)
			{
				if((pid = vfork()) < 0)
				{
					timed_log("!spawnbot -- error in fork!");
					break;
				}
				else if(!pid)
				{
					// This is the forked bot.
					execl(get_botexe().c_str(), get_botexe().c_str(), NULL);
					_exit(0);
				}
				// Server code continues:
				// It might be that the bot executable doesn't exists, in which
				// case the forked child just exited; check:
				if(waitpid(pid, &tmpi, WNOHANG) == pid && WIFEXITED(tmpi))
				{
					timed_log("!spawnbot -- could not find/run \'mrbrown\' executable!");
					Network::to_chat("Cannot spawn bots! Check configuration.");
					break;
				}
				// else bot is running and network will handle its connection attempt
			}
		}
		return false; // may broadcast
	case CMD_DROPBOTS: // req: AL >= 3 && not muted
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_ADMIN)
		{
			while(drop_a_bot()); // not fast, but simple
		}
		return false; // may broadcast
	case CMD_LOG: // req: AL >= 3
		if(pit->stats_i->ad_lvl >= AL_ADMIN && i != string::npos
			&& i < cmd.size()-1)
		{
			timed_log(pit->nick + " wrote: " + cmd.substr(i+1));
			return true; // do not broadcast succesful logging
		}
		return false; // may broadcast
	case CMD_PLINFO: // req: AL >= 2
		if(pit->stats_i->ad_lvl >= AL_TU
			&& i != string::npos && i < cmd.size()-1)
		{
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1));
			if(nit != cur_players.end())
			{
				// Construct player info:
				keyw = nit->nick + ": ";
				char host[80];
				char serv[20];
				if(getnameinfo((sockaddr*)&(nit->address), sizeof(nit->address),
					host, 80, serv, 20, NI_DGRAM|NI_NUMERICSERV))
					keyw += "[problem in getnameinfo]";
				else
				{
					keyw += host; keyw += ':'; keyw += serv;
				}
				if(nit->botpid != -1)
					keyw += ", BOT";
				keyw += ", t.time: ";
				keyw += lex_cast(nit->stats_i->total_time/60);
				keyw += "min";
				Network::construct_msg(keyw, cmd_respond_msg_col); 
				Network::send_to_player(*pit);
			}
		}
		return true; // no broadcast
	case CMD_PUTTEAM:
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_ADMIN)
		{
			// "!putteam [nick, may contain spaces!] [g/p/s]"
			size_t j = cmd.rfind(' ');
			if(j <= i+1 || j == cmd.size()-1) // invalid structure
				return false;
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1, j-i-1));
			if(nit != cur_players.end() && pit->stats_i->ad_lvl > nit->stats_i->ad_lvl)
			{
				switch(cmd[j+1])
				{
				case 'g': if(nit->team == T_PURPLE)
							Game::team_switch(nit);
						break;
				case 'p': if(nit->team == T_GREEN)
							Game::team_switch(nit);
						break;
				case 's': Game::class_switch(nit, NO_CLASS);
				//default: [ignore]
				}
			}
		}
		return false; // may broadcast
	case CMD_SETCLASS:
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_ADMIN)
		{
			// "!setclass [nick, may contain spaces!] [class abbr.]"
			size_t j = cmd.rfind(' ');
			if(j <= i+1 || j == cmd.size()-1) // invalid structure
				return false;
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1, j-i-1));
			if(nit != cur_players.end() && pit->stats_i->ad_lvl > nit->stats_i->ad_lvl)
			{
				cmd = cmd.substr(j+1); // this should be just the class abbr.
				// in case player gave "ar" instead of "Ar", for instance:
				cmd[0] = toupper(cmd[0]);
				for(j = 0; j < NO_CLASS; ++j)
				{
					if(cmd == classes[j].abbr)
					{
						Game::class_switch(nit, e_Class(j));
						break;
					}
				}
			}
		}
		return false; // may broadcast
	case CMD_HOWFAIR: // must be TU
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_TU)
		{
			if(num_players[T_GREEN]*num_players[T_PURPLE])
			{
				tmpi = 0;
				list<Player>::const_iterator it2;
				for(list<Player>::const_iterator it = cur_players.begin();
					it != cur_players.end(); ++it)
				{
					if(it->team == T_GREEN)
					{
						// Compute how many purple players this player "beats":
						for(it2 = cur_players.begin(); it2 != cur_players.end(); ++it2)
						{
							if(it2->team == T_PURPLE && pl_level_cmp(it, it2))
								++tmpi;
						}
					}
				}
				/* Now 'tmpi' is between 0 (all purples better than all greens) and
				 * ngreen*npurple (all greens better than all purples). */
				tmpi = 100*tmpi/(num_players[T_GREEN]*num_players[T_PURPLE]) - 50;
				keyw = lex_cast(tmpi) + " for greens -- the teams are ";
				if(tmpi < -35 || tmpi > 35)
					keyw += "VERY uneven!";
				else
				{
					if(tmpi < -15 || tmpi > 15)
						keyw += "not ";
					keyw += "even.";
				}
			}
			else // one or both teams is empty
				keyw = "Cannot compute evenness!";
			Network::to_chat(keyw);
		}
		return false; // may broadcast
	case CMD_LSCMD:
		if(!pit->muted)
		{
			keyw = "Enabled commands:";
			for(ind = 0; ind < MAX_SERVER_CMD; ++ind)
			{
				if(!Config::is_cmd_banned(ind))
				{
					// additional checks for various commands:
					if(ind == CMD_STOREMAP && Config::get_mapdir().empty())
						continue;
					if(ind == CMD_CLEARBANS && Config::is_cmd_banned(CMD_BAN))
						continue;
					if((ind == CMD_SPAWNBOTS || ind == CMD_DROPBOTS)
						&& Config::get_botexe().empty())
						continue;
					keyw += ' ' + server_cmd[ind];
				}
			}
			Network::to_chat(keyw);
		}
		return false; // may broadcast
	case CMD_SETCONF:
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_ADMIN
			&& i != string::npos && i < cmd.size()-1)
		{
			if(!Config::parse_config(cmd.substr(i+1), false))
			{
				keyw = "Configuration changed.";
				Network::to_chat(keyw);
			}
			return false;
		}
	default:
#ifdef DEBUG
		to_log("BUG: something wrong with command handling, keyw=\'" + keyw + '\'');
#endif
		return false;
	} // switch ind
}


void shuffle_teams()
{
	// Gather the players who are currently playing, and kill them all:
	list<list<Player>::iterator> playing_players;
	list<Player>::iterator it;
	for(it = cur_players.begin(); it != cur_players.end(); ++it)
	{
		if(it->team != T_SPEC)
		{
			it->own_vp->move_watchers();
			if(!intermission)
			{
				Game::del_owneds(it);
				if(it->is_alive())
					kill_player(it);
				it->own_vp->clear_memory();
			}
			playing_players.push_back(it);
		}
	}

	playing_players.sort(pl_level_cmp);

	char ch, num;
	e_Team t;
	num_players[T_GREEN] = num_players[T_PURPLE] = 0;
	while(!playing_players.empty())
	{
		if(playing_players.size() == 1)
		{
			++num_players[(playing_players.front()->team = e_Team(random()%2))];
			break; // done
		}
		// else
		if(playing_players.size() == 2)
			num = 1;
		else
			num = 1 + random()%2; // 1 or 2
		t = e_Team(random()%2);
		for(ch = 0; ch < num; ++ch)
		{
			playing_players.front()->team = t;
			++num_players[t];
			playing_players.pop_front();
		}
		t = (t == T_GREEN) ? T_PURPLE : T_GREEN;
		for(ch = 0; ch < num; ++ch)
		{
			playing_players.front()->team = t;
			++num_players[t];
			playing_players.pop_front();
			// This can happen if there's an odd number of players:
			if(playing_players.empty())
				break;
		}
	} // while playing_players not empty
	
	Network::to_chat("The teams have been shuffled!");
	Game::construct_team_msgs(cur_players.end());
	Game::send_team_upds(cur_players.end());
	for(it = cur_players.begin(); it != cur_players.end(); ++it)
		Game::send_state_change(it);
}


bool drop_a_bot()
{
	list<Player>::iterator bit = cur_players.begin();
	if(next_bot(bit))
	{
		Game::remove_player(bit, " dropped.");
		return true;
	}
	return false;
}


short num_bots()
{
	short num = 0;
	list<Player>::iterator bit = cur_players.begin();
	while(next_bot(bit))
	{
		++bit;
		++num;
	}
	return num;
}

#endif // not maptest build

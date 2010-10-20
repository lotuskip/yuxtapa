// Please see LICENSE file.
#include "cmds.h"
#include "server.h"
#include "log.h"
#include "game.h"
#include "network.h"
#include "players.h"
#include "settings.h"
#include "chores.h"
#include <cctype>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <algorithm>
#include <boost/lexical_cast.hpp>
#ifdef DEBUG
#include <iostream>
#endif

namespace
{
using namespace std;
using boost::lexical_cast;

const unsigned char cmd_respond_msg_col = 12;

const string ad_lvl_name[4] = { "guest", "regular", "trusted user",
	"admin" };
const string team_balance_str[] = { "no", "passive", "active" };

list<pid_t> botpids;


// For sorting players by their "level" for team shuffling.
// We want a *descending* order; return true if i should be before j.
// Note that this ordering is completely hidden from the players!
bool pl_level_cmp(const list<Player>::iterator i, const list<Player>::iterator j)
{
	// 1. Higher admin level wins.
	if(j->stats_i->ad_lvl != i->stats_i->ad_lvl)
		return (i->stats_i->ad_lvl > j->stats_i->ad_lvl);
	// 2. A consideration of kills/deaths & playing time:
	unsigned long resi = 0, resj = 0;
	for(char ch = 0; ch < NO_CLASS; ++ch)
	{
		resi += i->stats_i->time_played[ch];
		resj += j->stats_i->time_played[ch];
	}
	resi /= NO_CLASS; // averages of times played as each class
	resj /= NO_CLASS;
	// only take kill/death ratio into account if have enough deaths:
	if(i->stats_i->deaths >= 20)
		resi *= float(i->stats_i->kills)/i->stats_i->deaths;
	if(j->stats_i->deaths >= 20)
		resj *= float(j->stats_i->kills)/j->stats_i->deaths;
	// It might be that resi == resj, although this is very unlikely.
	// In that case, we just arbitrarily let j win:
	return (resi > resj);
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

}


bool process_cmd(const list<Player>::iterator pit, string &cmd)
{
	cmd.erase(0, 1); // remove '!'
	size_t i = cmd.find(' ');
	string keyw = cmd.substr(0, i);
	transform(keyw.begin(), keyw.end(), keyw.begin(), (int(*)(int))tolower);
	if(keyw == "serverinfo") // req: not muted
	{
		if(!pit->muted)
		{
			using namespace Config;
			string info = "Max players: "
				+ lexical_cast<string>(int_settings[IS_MAXPLAYERS])
				+ ", mapsize ";
			short minmsize = (100 - int_settings[IS_MAPSIZEVAR])
				*int_settings[IS_MAPSIZE]/100;
			short maxmsize = (100 + int_settings[IS_MAPSIZEVAR])
				*int_settings[IS_MAPSIZE]/100;
			info += lexical_cast<string>(minmsize) + '-'
				+ lexical_cast<string>(maxmsize) + ", classlim ";
			info += lexical_cast<string>(int_settings[IS_CLASSLIMIT])
				+ ", turn " + lexical_cast<string>(int_settings[IS_TURNMS]);
			info += "ms, intermission "
				+ lexical_cast<string>(int_settings[IS_INTERM_SECS]) + "s,";
			Network::to_chat(info);
			info = "statpurge "
				+ lexical_cast<string>(int_settings[IS_STATPURGE]) + "h, "
				+ team_balance_str[int_settings[IS_TEAMBALANCE]];
			info += " teambalance, modes: " + game_modes_str();
			Network::to_chat(info);
			// may broadcast
		}
	}
	else if(keyw == "adminlvl") // req: none
	{
		if(i != string::npos && i < cmd.size()-1)
		{
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1));
			if(nit != cur_players.end())
			{
				keyw = nit->nick + " has admin level "
					+ lexical_cast<string>(short(nit->stats_i->ad_lvl))
					+ admin_lvl_str[nit->stats_i->ad_lvl];
				Network::construct_msg(keyw, cmd_respond_msg_col); 
				Network::send_to_player(*pit);
			}
		}
		return true; // a "private" request
	}
	else if(keyw == "teams") // req: none
	{
		Game::construct_team_msgs(pit);
		return true; // a "private" request
	}
	else if(keyw == "shuffle") //req: AL >= 2 && not muted
	{
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_TU)
			shuffle_teams();
		// may broadcast
	}
	else if(keyw == "nextmap") // req: AL >= 2 && not muted
	{
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_TU)
			next_map_forced();
		// may broadcast
	}
	else if(keyw == "teambal") // req: AL >= 3 && not muted
	{
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_ADMIN
			&& i < cmd.size()-1)
		{
			using namespace Config;
			switch(cmd[i+1])
			{
			case 'a': case 'A':
				int_settings[IS_TEAMBALANCE] = TB_ACTIVE; break;
			case 'p': case 'P':
				int_settings[IS_TEAMBALANCE] = TB_PASSIVE; break;
			case 'o': case 'O':
				int_settings[IS_TEAMBALANCE] = TB_OFF; break;
			default: return false;
			}
			string info = "Team balance set to: \""
				+ team_balance_str[int_settings[IS_TEAMBALANCE]] + "\".";
			Network::to_chat(info);
			// may broadcast
		}
	}
	else if(keyw == "kick") // req: AL > max(1, target's) && not muted
	{
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_TU && i != string::npos
			&& i < cmd.size()-1)
		{
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1));
			if(nit != cur_players.end() &&
				pit->stats_i->ad_lvl > nit->stats_i->ad_lvl)
				Game::remove_player(nit, " kicked by " + pit->nick + '.');
			// may broadcast
		}
	}
	else if(keyw == "mute") // req: AL > target's && not muted
	{
		if(!pit->muted && i != string::npos && i < cmd.size()-1)
		{
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1));
			if(nit != cur_players.end() && !nit->muted &&
				pit->stats_i->ad_lvl > nit->stats_i->ad_lvl)
			{
				nit->muted = true;
				string msg = "You have been muted by " + pit->nick + '.';
				Network::construct_msg(msg, cmd_respond_msg_col);
				Network::send_to_player(*nit);
			}
			// may broadcast
		}
	}
	else if(keyw == "unmute") // req: AL > target's && not muted
	{
		if(!pit->muted && i != string::npos && i < cmd.size()-1)
		{
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1));
			if(nit != cur_players.end() && nit->muted &&
				pit->stats_i->ad_lvl > nit->stats_i->ad_lvl)
			{
				nit->muted = false;
				string msg = "You were unmuted by " + pit->nick + '.';
				Network::construct_msg(msg, cmd_respond_msg_col);
				Network::send_to_player(*nit);
			}
			// may broadcast
		}
	}
	else if(keyw == "setal")
	{
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
							+ lexical_cast<string>(known_players.front().ID) + '.';
						Network::construct_msg(keyw, cmd_respond_msg_col);
						Network::send_to_player(*pit);
						return true;
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
		} // command valid for !setal
	}
	else if(keyw == "ban") // req: AL > max(2, target's) && not muted
	{
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_ADMIN
			&& i != string::npos && i < cmd.size()-1)
		{
			list<Player>::iterator nit = id_pl_by_nick_part(cmd.substr(i+1));
			if(nit != cur_players.end() &&
				pit->stats_i->ad_lvl > nit->stats_i->ad_lvl)
			{
				banlist().push_back(nit->address);
				Game::remove_player(nit, " banned by " + pit->nick + '.');
			}
			// may broadcast
		}
	}
	else if(keyw == "servermsg") // req: AL >= 3 && not muted
	{
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_ADMIN && i != string::npos)
		{
			if(i < cmd.size()-1)
				Config::set_greeting_str(cmd.substr(i+1));
			else
				Config::set_greeting_str("");
			// may broadcast
		}
	}
	else if(keyw == "clearbans") // req: AL >= 3 && not muted
	{
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_ADMIN)
			banlist().clear();
		// may broadcast
	}
	else if(keyw == "spawnbots") // req: AL >= 3 && not muted
	{
		using namespace Config;
		if(!get_botexe().empty() && pit->stats_i->ad_lvl >= AL_ADMIN
			&& !pit->muted && i < cmd.size()-1 && isdigit(cmd[i+1]))
		{
			// Spawn as many as requested, as long as that won't make the
			// server over-full:
			char num = min(int(cmd[i+1]-'0'),
				int(int_settings[IS_MAXPLAYERS] - cur_players.size()));
			pid_t pid;
			int ces;
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
				if(waitpid(pid, &ces, WNOHANG) == pid && WIFEXITED(ces))
				{
					timed_log("!spawnbot -- could not find/run \'mrbrown\' executable!");
					Network::to_chat("Cannot spawn bots! Mr. Brown executable most likely missing...");
					break;
				}
				// else bot is running:
				botpids.push_back(pid);
			}
			// may broadcast
		}
	}
	else if(keyw == "dropbots") // req: AL >= 3 && not muted
	{
		if(!pit->muted && pit->stats_i->ad_lvl >= AL_ADMIN)
		{
			for(list<pid_t>::const_iterator it = botpids.begin();
				it != botpids.end(); ++it)
				kill(*it, SIGINT);
			botpids.clear();
			// may broadcast
		}
	}
	else if(keyw == "dontstatme")
	{
		pit->stats_i->bot = true;
		return true; // private request
	}
	return false;
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
			Game::del_owneds(it);
			if(it->is_alive())
				kill_player(it);
			playing_players.push_back(it);
		}
	}

	playing_players.sort(pl_level_cmp);

	char ch, num;
	e_Team t;
	extern unsigned char num_players[2]; // defined in game.cpp
	num_players[0] = num_players[1] = 0;
	while(!playing_players.empty())
	{
		if(playing_players.size() == 1)
		{
			playing_players.front()->team = e_Team(T_GREEN + random()%2);
			break; // done
		}
		// else
		if(playing_players.size() == 2)
			num = 1;
		else
			num = 1 + random()%2; // 1 or 2
		t = e_Team(T_GREEN + random()%2);
		for(ch = 0; ch < num; ++ch)
		{
			playing_players.front()->team = t;
			num_players[(t == T_PURPLE)]++;
			playing_players.pop_front();
		}
		if(t == T_GREEN)
			t = T_PURPLE;
		else
			t = T_GREEN;
		for(ch = 0; ch < num; ++ch)
		{
			playing_players.front()->team = t;
			num_players[(t == T_PURPLE)]++;
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
	if(!num_bots())
		return false; // no bots to drop
	kill(botpids.front(), SIGINT);
	botpids.pop_front();
	return true;
}


short num_bots()
{
	int ces;
	// First, remove from the list those bots who are there no longer:
	list<pid_t>::iterator bit = botpids.begin();
	while(bit != botpids.end())
	{
		if(waitpid(*bit, &ces, WNOHANG) == *bit
			&& (WIFEXITED(ces) || WIFSIGNALED(ces)))
			bit = botpids.erase(bit);
		else
			++bit;
	}
	return botpids.size();
}


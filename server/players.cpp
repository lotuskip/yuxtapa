// Please see LICENSE file.
#ifndef MAPTEST
#include "settings.h"
#include "viewpoint.h"
#include "log.h"
#include "cmds.h"
#include "../common/utfstr.h"
#include "../common/util.h"
#include <netinet/in.h>
#include <fstream>
#include <iostream>
#include <cctype>
#include <cstring>
#include <algorithm>

namespace Game
{
extern unsigned short curturn;
}

bool operator==(const PlayerStats &ps1, const PlayerStats &ps2)
{
	return ps1.ID == ps2.ID;
}

namespace
{
using namespace std;

// The statistic versions run backwards from -1 (this assures "compatability"
// with IAv1 branch where there is no statistics version stored).
const short STAT_VERSION = -1;

const string playerfilename = ".playerdata";

const string kill_way_name[MAX_WAY_TO_KILL] = { "melee", "arrow",
	"slice", "zap", "boobytrap", "fireball", "mm", "backstab",
	"squash", "poison", "scare to death" };

// We remember at most this many players. A limit exists because if the
// server settings are such that players are never forgotten, we might
// _eventually_ have so many players it becomes a drag to find free
// ID numbers for new ones...
const short MAX_STORED_PLAYERS = 10000;

const string digitstr = "0123456789";

const char NUM_FLASHBOMBS = 3;
const short TORCH_DURATION = 500; // torches last 500--999 turns
const char torch_sym[4] = { ';', 'i', 'j', 'J' };

vector<short> usage_per_30min;

unsigned short next_ID_to_give = 0;

void register_player(string nick, sockaddr_storage &sas,
	list<PlayerStats>::iterator it, const bool is_bot)
{
	// Check if nick is taken and modify if needed:
	list<Player>::const_iterator i = cur_players.begin();
	while(i != cur_players.end())
	{
		if(i->nick == nick)
		{
			unsigned short found = 0;
			bool twodigits = false;
			if(!nick.empty() && isdigit(nick[nick.size()-1]))
			{
				found += int(nick[nick.size()-1]-'0');
				nick.erase(nick.end()-1);
			}
			if(!nick.empty() && isdigit(nick[nick.size()-1]))
			{
				found += 10*int(nick[nick.size()-1]-'0');
				nick.erase(nick.end()-1);
				twodigits = true;
			}
			string numpart;
			++found;
			if(found == 100)
				numpart = "01";
			else
				numpart = lex_cast(found);
			if(twodigits && numpart.size() == 1)
				numpart.insert(0, 1, '0');

			while(num_syms(nick) + numpart.size() > MAX_NICK_LEN)
				del(nick, num_syms(nick)-1);

			nick += numpart;
			// Nick was modified, so we must restart from the beginning (the
			// modified nick might now conflict with an earlier checked nick!)
			i = cur_players.begin();
		}
		else
			++i;
	}
	list<Player>::iterator npit = cur_players.insert(cur_players.end(),
		Player());
	npit->stats_i = it;
	time(&(npit->stats_i->last_seen));
	npit->nick = it->last_known_as = nick;
	npit->address = sas;
	npit->muted = npit->needs_state_upd = false;
	npit->poisoner = cur_players.end();
	npit->clear_acts();
	npit->last_heard_of = Game::curturn;
	npit->facing = D_N; // just to give a definite value
	npit->sector = npit->wants_to_move_to = MAX_D;
	// a newly connected player is a spectator:
	npit->team = T_SPEC;
	npit->cl = npit->next_cl = NO_CLASS;
	npit->cl_props.hp = // this assures the player is !alive()
		npit->doing_a_chore =
		npit->warned_of_chasm = 0;
	npit->botpid = -1;

	// A newly connected player is watching her own viewpoint:
	(npit->own_vp = new ViewPoint())->set_owner(npit);
	npit->own_vp->add_watcher(npit);
	npit->viewn_vp = npit->own_vp;

	if(!is_bot && Config::int_settings[Config::IS_MINPLAYERS])
	{
		// a newly connected player (including a bot) can replace a bot:
		if(cur_players.size() == Config::int_settings[Config::IS_MINPLAYERS]+1)
			drop_a_bot(); // if no bots, okay
		// or this might be our first human player?
		else if(cur_players.size() == 1)
		{
			// players won't get added to cur_players until later, so
			// record locally how many have been added
			unsigned short np = 1;
			while(np < Config::int_settings[Config::IS_MINPLAYERS]
				&& !spawn_bot())
				++np;
		}
	}
}


list<PlayerStats>::iterator new_player()
{
	PlayerStats tmpstats;
	// determine a free ID for the new player:
	tmpstats.ID = next_ID_to_give;
	while(find(known_players.begin(), known_players.end(), tmpstats)
		!= known_players.end())
	{
		if(++(tmpstats.ID) == HIGHEST_VALID_ID)
			tmpstats.ID = 0;
	}
	next_ID_to_give = tmpstats.ID+1;
	// Generate password:
	for(char ch = 0; ch < PASSW_LEN; ++ch)
		tmpstats.password[ch] = char(random()%94+33); // this gives printing ASCII
	tmpstats.password[PASSW_LEN] = '\0';

	return known_players.insert(known_players.end(), tmpstats);
}


// Check if two addresses are equal (IP-wise only)
bool equal_ip(const sockaddr_storage *first, const sockaddr_storage *second)
{
	if(first->ss_family == second->ss_family)
	{
		if(first->ss_family == AF_INET)
			return !memcmp(&(((const sockaddr_in*)first)->sin_addr),
				&(((const sockaddr_in*)second)->sin_addr), sizeof(in_addr));
		// else address family is AF_INET6
		return !memcmp(&(((const sockaddr_in6*)first)->sin6_addr),
				&(((const sockaddr_in6*)second)->sin6_addr), sizeof(in6_addr));
	}
	return false; // one is IPv4, other IPv6
}

// to check if two addresses are equal (IP & port)
bool equal_sas(const sockaddr_storage *first, const sockaddr_storage *second)
{
	if(!equal_ip(first, second))
		return false;
	// else check ports. (ss_families are same if IPs are)
	if(first->ss_family == AF_INET)
		return ((const sockaddr_in*)first)->sin_port
			== ((const sockaddr_in*)second)->sin_port;
	// else AF_INET6
	return ((const sockaddr_in6*)first)->sin6_port
		== ((const sockaddr_in6*)second)->sin6_port;
}

list<Player>::const_iterator find_pl_by_sas(const sockaddr_storage &sas)
{
	list<Player>::const_iterator i = cur_players.begin();
	for(; i != cur_players.end(); ++i)
	{
		if(equal_sas(&(i->address), &sas))
			break;
	}
	return i;
}

unsigned short add_and_register_player(const string &nick, string &passw,
	sockaddr_storage &sas, const bool is_bot)
{
	list<PlayerStats>::iterator newpl = new_player();
	register_player(nick, sas, newpl, is_bot);
	passw.assign(newpl->password);
	return newpl->ID;
}

} // end local namespace

list<PlayerStats> known_players;
list<Player> cur_players;

list<sockaddr_storage> banned_ips;


PlayerStats::PlayerStats() : last_known_as(""), deaths(0), tks(0),
	healing_recvd(0), total_time(0), time_specced(0), arch_hits(0),
	arch_shots(0), cm_hits(0), cm_shots(0), ad_lvl(AL_GUEST)
{
	int i;
	for(i = 0; i < NO_CLASS; ++i)
		time_played[i] = 0;
	for(i = 0; i < MAX_WAY_TO_KILL; ++i)
		kills[i] = 0;
}


Player::~Player() { delete own_vp; }


void Player::clear_acts()
{
	last_acted_on_turn = turns_without_axn = last_axn_number = wait_turns = 0;
	acted_this_turn = false; 
}

bool Player::is_alive() const
{
	return team != T_SPEC // specs aren't even playing!
		&& cl_props.hp > 0;
}

void Player::set_class()
{
	cl_props = classes[(cl = next_cl)];
	limiter = (cl == C_ASSASSIN) ? NUM_FLASHBOMBS : 0;
	time(&last_switched_cl);
}


void Player::init_torch()
{
	if(cl_props.torch)
		torch_left = TORCH_DURATION + rand()%TORCH_DURATION;
	else
		torch_left = 0;
}

bool Player::burn_torch()
{
	--torch_left;
	if(torch_left == 3*TORCH_DURATION/2 || torch_left == TORCH_DURATION/2
		|| torch_left == TORCH_DURATION || !torch_left)
		needs_state_upd = true;
	return !torch_left;
}

unsigned char Player::torch_symbol(const bool lit) const
{
	if(cl_props.torch)
	{
		if(!torch_left)
			return ',';
		if(lit)
			return torch_sym[2*torch_left/TORCH_DURATION];
		return ' '; // torch not lit
	}
	return '-'; // no torch whatsoever
}

bool Player::nomagicres() const
{
	return (random()%100 >= cl_props.magicres);
}

/////////////////////////////////////////////////////////


// The nopurge argument indicates we are running the server with "--pstats"
void init_known_players(const bool nopurge)
{
	string fname = Config::get_config_dir() + playerfilename;
	ifstream file(fname.c_str(), ios_base::binary);
	if(!file)
	{
		to_log("Could not read player data; file missing or not readable.");
		return;
	}

	short numpl;
	file.read(reinterpret_cast<char*>(&numpl), sizeof(short));
	if(numpl != STAT_VERSION)
	{
		to_log("Wrong statistics version in player data! All statistics will be cleared!");
		return;
	}

	file.read(reinterpret_cast<char*>(&numpl), sizeof(short));
	if(numpl < 0 || numpl > MAX_STORED_PLAYERS)
	{
		to_log("Player datafile is corrupt! Ignoring the data!");
		return;
	}
	if(!nopurge)
		to_log("Player data contains " + lex_cast(numpl)
			+ " entries;");
	PlayerStats tmpstats;
	short sh; char ch;
	tmpstats.password[PASSW_LEN] = '\0';
	int tooold;
	if(nopurge)
		tooold = 0;
	else // statpurge is hours, tooold seconds:
		tooold = 60*60*Config::int_settings[Config::IS_STATPURGE];
	while(numpl--)
	{
		file.read(reinterpret_cast<char*>(&tmpstats.ID), sizeof(short));
		file.read(tmpstats.password, PASSW_LEN);
		file.read(reinterpret_cast<char*>(tmpstats.kills), MAX_WAY_TO_KILL*sizeof(long));
		file.read(reinterpret_cast<char*>(&tmpstats.deaths), sizeof(long));
		file.read(reinterpret_cast<char*>(&tmpstats.tks), sizeof(long));
		file.read(reinterpret_cast<char*>(&tmpstats.healing_recvd), sizeof(long));
		file.read(reinterpret_cast<char*>(&tmpstats.total_time), sizeof(long));
		file.read(reinterpret_cast<char*>(&tmpstats.time_specced), sizeof(long));
		file.read(reinterpret_cast<char*>(tmpstats.time_played), NO_CLASS*sizeof(long));
		file.read(reinterpret_cast<char*>(&tmpstats.arch_hits), sizeof(long));
		file.read(reinterpret_cast<char*>(&tmpstats.arch_shots), sizeof(long));
		file.read(reinterpret_cast<char*>(&tmpstats.cm_hits), sizeof(long));
		file.read(reinterpret_cast<char*>(&tmpstats.cm_shots), sizeof(long));
		file.read(reinterpret_cast<char*>(&tmpstats.ad_lvl), sizeof(e_AdminLvl));
		file.read(reinterpret_cast<char*>(&tmpstats.last_seen), sizeof(time_t));
		tmpstats.last_known_as.clear();
		for(file.read(reinterpret_cast<char *>(&sh), sizeof(short)); sh > 0; --sh)
		{
			file.read(reinterpret_cast<char *>(&ch), sizeof(char));
			tmpstats.last_known_as += ch;
		}
		if(!tooold || tmpstats.ad_lvl > AL_REG || (time(NULL) - tmpstats.last_seen) < tooold)
			known_players.push_back(tmpstats);
	}
	if(!nopurge)
		to_log(lex_cast(known_players.size()) + " entries accepted.");
}

void store_known_players()
{
	// This function is called when the server shuts down. Now, it might be that
	// there were players connected at that time, and we need to stat their
	// playing times here!
	extern bool intermission;
	for(list<Player>::iterator pit = cur_players.begin();
		pit != cur_players.end(); ++pit)
	{
		if(pit->botpid != -1) // it's a bot
			known_players.erase(pit->stats_i);
		else
		{
			pit->stats_i->total_time += time(NULL) - pit->stats_i->last_seen;
			time(&(pit->stats_i->last_seen));
		
			if(pit->is_alive())
				pit->stats_i->time_played[pit->cl] += time(NULL) - pit->last_switched_cl;
			else if(pit->team == T_SPEC && !intermission)
				pit->stats_i->time_specced += time(NULL) - pit->last_switched_cl;
		}
	}

	// Now start actually storing the player statistics:
	write_player_stats();
}

void write_player_stats()
{
	string fname = Config::get_config_dir() + playerfilename;
	ofstream file(fname.c_str(), ios_base::binary);
	if(!file)
	{
		to_log("ERROR: could not open player data for writing!");
		return;
	}

	file.write(reinterpret_cast<const char*>(&STAT_VERSION), sizeof(short));

	short numpl;
	if((signed int)(known_players.size()) > MAX_STORED_PLAYERS)
		numpl = MAX_STORED_PLAYERS;
	else // this is done like this 'cuz basically the size can be more than a short fits
		numpl = known_players.size();
	// Note that when called between maps we are writing bots' stats into the
	// file, too!
	file.write(reinterpret_cast<char*>(&numpl), sizeof(short));
	for(list<PlayerStats>::iterator it = known_players.begin();
		it != known_players.end(); ++it)
	{
		file.write(reinterpret_cast<char*>(&it->ID), sizeof(short));
		file.write(known_players.front().password, PASSW_LEN);
		file.write(reinterpret_cast<char*>(it->kills), MAX_WAY_TO_KILL*sizeof(long));
		file.write(reinterpret_cast<char*>(&it->deaths), sizeof(long));
		file.write(reinterpret_cast<char*>(&it->tks), sizeof(long));
		file.write(reinterpret_cast<char*>(&it->healing_recvd), sizeof(long));
		file.write(reinterpret_cast<char*>(&it->total_time), sizeof(long));
		file.write(reinterpret_cast<char*>(&it->time_specced), sizeof(long));
		file.write(reinterpret_cast<char*>(it->time_played), NO_CLASS*sizeof(long));
		file.write(reinterpret_cast<char*>(&it->arch_hits), sizeof(long));
		file.write(reinterpret_cast<char*>(&it->arch_shots), sizeof(long));
		file.write(reinterpret_cast<char*>(&it->cm_hits), sizeof(long));
		file.write(reinterpret_cast<char*>(&it->cm_shots), sizeof(long));
		file.write(reinterpret_cast<char*>(&it->ad_lvl), sizeof(e_AdminLvl));
		file.write(reinterpret_cast<char*>(&it->last_seen), sizeof(time_t));
		numpl = it->last_known_as.size();
		file.write(reinterpret_cast<char *>(&numpl), sizeof(short));
		file.write(it->last_known_as.c_str(), numpl);
	}
}

unsigned short player_hello(const unsigned short id, string &passw,
	const string &nick, sockaddr_storage &sas, const bool is_bot)
{
	/* First check if this client is already registered, because the same
	 * client might send multiple hellos having lost the reply. We don't want
	 * to first accept them and then kick them out because they didn't hear
	 * our first reply. */
	list<Player>::const_iterator it = find_pl_by_sas(sas);
	if(it != cur_players.end())
	{
		// we have already replied; figure out what:
		if(it->stats_i->ID == id)
			return JOIN_OK_ID_OK;
		// else we assigned them a new id already
		passw.assign(it->stats_i->password);
		return it->stats_i->ID;
	}

	// Check if IP is banned:
	for(list<sockaddr_storage>::const_iterator sit = banned_ips.begin();
		sit != banned_ips.end(); ++sit)
	{
		if(equal_ip(&sas, &(*sit)))
			return IP_BANNED;
	}

	// Check server being at full capacity:
	if(cur_players.size() >= Config::int_settings[Config::IS_MAXPLAYERS])
	{
		// There are too many players, but some of them might be bots.
		if(!drop_a_bot()) // no bots to drop!
			return SERVER_FULL;
		// else a bot was dropped and there is room again, proceed:
	}

	if(id == 0xFFFF) // a new player
		return add_and_register_player(nick, passw, sas, is_bot);

	// Check if ID is already playing:
	for(it = cur_players.begin(); it != cur_players.end(); ++it)
	{
		if(it->stats_i->ID == id)
			return ID_STEAL;
	}

	// Check if we actually know this player like they claim:
	list<PlayerStats>::iterator si;
	for(si = known_players.begin(); si != known_players.end(); ++si)
	{
		if(si->ID == id) // it is a returning customer
		{
			if(passw != si->password) // received wrong password!
				return ID_STEAL;
			// else all is good:
			register_player(nick, sas, si, is_bot);
			return JOIN_OK_ID_OK;
		}
	}
	// If here, we have forgotten about this player, most likely
	return add_and_register_player(nick, passw, sas, is_bot);
}

unsigned short bot_connect(const unsigned short pid, sockaddr_storage &sas)
{
	// Basically we delegate to player_hello, but some extra checks:
	// We don't want to "drop bots to make room for bots":
	if(cur_players.size() >= Config::int_settings[Config::IS_MAXPLAYERS])
		return SERVER_FULL;
	// else:
	string tmp1, tmp2 = Config::new_bot_name();
	unsigned short sh = player_hello(0xFFFF, tmp1, tmp2, sas, true);
	if(sh <= HIGHEST_VALID_ID) // player was added
		cur_players.back().botpid = pid;
	return sh;
}

void usage_update()
{
	if(!cur_players.empty())
	{
		short n = cur_players.size();
		short nbots = num_bots();
		usage_per_30min.push_back(n - nbots);
		string msg = lex_cast(n - nbots) + " players";
		if(nbots)
			msg += " (+ " + lex_cast(nbots) + " bots)";
		timed_log(msg);
	}
}


void usage_report()
{
	if(!usage_per_30min.empty())
	{
		// compute average:
		int sum = 0;
		for(vector<short>::const_iterator i = usage_per_30min.begin();
			i != usage_per_30min.end(); ++i)
			sum += *i;
		to_log("Average presence: " +
			lex_cast_fl(float(sum)/usage_per_30min.size()));
		usage_per_30min.clear();
	}
}


void output_stats()
{
	// General stuff:
	cout << "Players: " << known_players.size() << endl << endl;

	struct tm *timeinfo;
	unsigned long l;
	short sh;
	for(list<PlayerStats>::const_iterator it = known_players.begin();
		it != known_players.end(); ++it)
	{
		cout << it->last_known_as << endl;
		cout << "AL " << short(it->ad_lvl) << endl;
 		timeinfo = localtime(&(it->last_seen));
		cout << "seen " << asctime(timeinfo); // asctime includes '\n'
		l = it->total_time;
		cout << "tot_time: " << l/(60*60) << ':';
		if((l%(60*60))/60 < 10)
			cout << '0';
		cout << (l%(60*60))/60 << ':';
		if(l%60 < 10)
			cout << '0';
		cout << l%60 << endl;
		if(l)
		{
			cout << "as_spec: " << 100*it->time_specced/l << '%' << endl;
			for(sh = 0; sh < NO_CLASS; ++sh)
			{
				if(100*it->time_played[sh]/l) // only print non-zeros here
					cout << "as_" << classes[sh].abbr << ": "
						<< 100*it->time_played[sh]/l << '%' << endl;
			}
		}
		l = 0;
		cout << "kills: ";
		for(sh = 0; sh < MAX_WAY_TO_KILL; ++sh)
		{
			if(it->kills[sh])
			{
				cout << it->kills[sh]  << ' ' << kill_way_name[sh] << ", ";
				l += it->kills[sh];
			}
		}
		cout << "TOTAL: " << l << endl;
		cout << "t-kills: " << it->tks << endl;
		cout << "deaths: " << it->deaths << endl;
		cout << "healing: " << it->healing_recvd << endl;
		cout << "arch: ";
		if(it->arch_shots)
			cout  << 100*it->arch_hits/(it->arch_shots) << '%';
		else
			cout << '-';
		cout << endl << "zaps: ";
		if(it->cm_shots)
			cout << 100*it->cm_hits/(it->cm_shots) << '%';
		else
			cout << '-';
		cout << endl << endl;
	}
}


std::list<sockaddr_storage> &banlist() { return banned_ips; }


bool next_bot(list<Player>::iterator &it)
{
	while(it != cur_players.end() && it->botpid == -1)
		++it;
	return (it != cur_players.end());
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

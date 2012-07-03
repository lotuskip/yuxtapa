// Please see LICENSE file.
#include "../common/netutils.h"
#include "../common/timer.h"
#include "../common/classes_common.h"
#include "../common/coords.h"
#include "../common/col_codes.h"
#include "../common/los_lookup.h"
#include "../common/util.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <iostream>
#include <algorithm>

using namespace std;

// CONSTANTS
/////////////
const Coords center(VIEWSIZE/2, VIEWSIZE/2);
const string unpassables = "@+O"; // TODO: add here if some others become relevant
// a CHANCE is a chance of something happening. Usually these are
// "1IN" chances, meaning that there is a "1 in x" chance of
// something happening.
const char CHANCE_IGN_CL_SPC[NO_CLASS] = {
	// chances, in %, of not even trying to use special ability
	10 /*Ar*/, 10 /*As*/, 15 /*Cm*/, 33 /*Mc*/, 10 /*Sc*/, 15 /*Fi*/,
	15 /*Mi*/, 10 /*He*/, 10 /*Wi*/, 30 /*Tr*/, 100 /*Pw*/
};
const char WAIT_ON_RANDOM_WALK_1IN = 4; // wait when would otherwise walk randomly
const char WALK_BLIND_CHANCE_1IN = 7; // walking around blind
const char TURN_WITHOUT_REASON_1IN = 50; // turning when walking randomly
const char FOLLOW_ANYONE_1IN = 4; // following non-scout, non-miner teammates
// LIMITs mean how many turns a bot has to spend doing *something else* after
// using their special ability (so a higher number limits the usage of the ability more)
const char HEAL_POISON_LIMIT = 5; // after heal/poison others
const char HEAL_SELF_LIMIT = 3; // after heal self
const char TRAP_LIMIT = 40; // after setting/disarming a trap
const char FLASH_LIMIT = 8; // using flash bombs (assassins)
const char ZAP_LIMIT = 4; // zapping (combat magi)
// WAITs tell how many turns a bot has to spend *doing nothing at all* after
// using a special ability. These are often connected to limiting in the server
// end or to prevent the bot from interrupting a chore it itself started.
const char CAST_MM_WAIT = 2; // magic missile (wizards)
const char BLINK_WAIT = 1; // blink (mindcrafters)
const char CIRCLE_ATTACK_WAIT = 3; // circle attack (fighters)
const char DIG_WAIT = 7; // mining (miners)
const char TRAP_WAIT = 8; // trap setting/disarming (trappers)
const char ZAP_WAIT = 2; // zaps (combat magi)
const char DISGUISE_WAIT = 8; // disguising (scouts)
// Other stuff:
const char AIM_TURNS = 4; // how many turns must an archer take aim before firing
const char WALLS_FOR_CERTAIN_DIG = 38; // how many walls in sight ensure that miners will dig when possible
const short MSG_DELAY_MS = 10000; // wait for 10ms between checking for server messages
const short ISOLATION_TO_SUICIDE = 360; // if no enemies in sight for this many turns, suicide


// VARIABLES
//////////////
// Networking and/or "yuxtapa protocol" related:
SerialBuffer recv_buffer, send_buffer;
int s_me; // socket
struct addrinfo *servinfo, *the_serv;
sockaddr them;
socklen_t addr_len;
msTimer last_sent_axn, reftimer;
unsigned short cur_id;
unsigned short turn_ms;
unsigned char mid;
// Directly game related:
unsigned short curturn = 0;
unsigned char axn_counter = 0;
char wait_turns = 0;
char myhp = 0;
e_Team myteam = T_GREEN;
e_Class myclass = NO_CLASS;
char gm_dom1_con0 = -1; // to tell if gamemode is dom or con
char num_enemy_flags = 0;
char viewbuffer[BUFFER_SIZE];
bool torchlit = true;
// AI related:
e_Dir prev_committed_walk; // direction of last step taken
vector<Coords> pcs[2]; // PCs of each team currently in view
char limiter = 0; // used to prevent bots from repeating the same action too often
char abil_counter = 0; // used for various ability-related counting needs
Coords seen_flag_coords; // if a flag has been spotted
e_Team seen_flag_owner = T_SPEC;
unsigned char num_walls_in_sight = 0;
short isolation_turns;
char symbol_under_feet = 0; // what the bot thinks it is standing on
char col_under_feet = 0;
// Generic:
Coords tmp_coords;
int rv; // "return value". This is used as a _very volatile_ temp variable

/// Networking related functions:
//////////////////////////////////

bool do_send()
{
	return sendto(s_me, send_buffer.getr(), send_buffer.amount(), 0,
		the_serv->ai_addr, the_serv->ai_addrlen) == -1;
}

void send_action(const unsigned char xncode, const unsigned char var1 = 0, const unsigned char var2 = 0)
{
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_TAKE_ACTION);
	send_buffer.add(cur_id);
	send_buffer.add(curturn);
	send_buffer.add(axn_counter);
	send_buffer.add(xncode);
	if(xncode == XN_MOVE || xncode == XN_SHOOT || xncode == XN_ZAP
		|| xncode == XN_CIRCLE_ATTACK || xncode == XN_HEAL || xncode == XN_MINE)
	{
		send_buffer.add(static_cast<unsigned char>(var1));
		if(xncode == XN_SHOOT)
			send_buffer.add(static_cast<unsigned char>(var2));
		else if(xncode == XN_MOVE) // store where we think we step on
		{
			Coords c = center.in(e_Dir(var1));
			if(unpassables.find((rv = viewbuffer[(c.y*VIEWSIZE+c.x)*2+1])) == string::npos)
			{
				if((symbol_under_feet = rv) == '~')
					wait_turns = 2; // swim properly
				else if((symbol_under_feet == ';' || symbol_under_feet == '\"')
					&& myclass != C_TRAPPER && myclass != C_SCOUT)
					wait_turns = 1; // don't rush through rough terrain
				col_under_feet = viewbuffer[(c.y*VIEWSIZE+c.x)*2];
			}
		}
	}
	do_send();
	++axn_counter;
}

short do_receive()
{
	addr_len = sizeof(them);
	return recvfrom(s_me, recv_buffer.getw(), BUFFER_SIZE, 0, &them, &addr_len);
}

// returns true if something wrong
bool do_connect(const string &sip)
{
	string port = "", ip = "";
	size_t i;
	if((i = sip.rfind(':')) != string::npos && i > 1 && i != sip.size()-1)
	{
		port = sip.substr(i+1);
		ip = sip.substr(0, i);
		if(ip[0] == '[') // this would indicate IPv6
		{
			ip.erase(0,1);
			if(ip.empty())
			{
#ifdef BOTMSG
				cerr << "Not a valid address: " << sip << endl;
#endif
				return true;
			}
			ip.erase(ip.size()-1,1); // erase assumed ']'
		}
	}
	else
	{
#ifdef BOTMSG
		cerr << "Not a valid address: " << sip << endl;
#endif
		return true;
	}
	// create the socket
	struct addrinfo hints;
	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	// get server info:
	if((rv = getaddrinfo(ip.c_str(), port.c_str(), &hints, &servinfo)))
	{
#ifdef BOTMSG
		cerr << "Error in getaddrinfo: " << gai_strerror(rv) << endl;
#endif
		return true;
	}
	// initialize the_serv and make the socket based on its protocol info:
	for(the_serv = servinfo; the_serv; the_serv = the_serv->ai_next)
	{
		if((s_me = socket(the_serv->ai_family, the_serv->ai_socktype, the_serv->ai_protocol)) != -1)
			break;
	}
	if(!the_serv)
	{
#ifdef BOTMSG
		cerr << "Failed to create socket!" << endl;
#endif
		freeaddrinfo(servinfo);
		return true;
	}
	// set socket to nonblock mode:
	fcntl(s_me, F_SETFL, O_NONBLOCK);

	// construct a hello message: (different for bots than people)
	send_buffer.add((unsigned char)MID_BOTHELLO);
	send_buffer.add(INTR_VERSION);
	pid_t mypid = getpid();
	send_buffer.add((unsigned short)mypid);
	
	last_sent_axn.update();

	if(do_send())
	{
#ifdef BOTMSG
		cerr << "Problem connecting to \'" << sip << "\'." << endl;
#endif
		close(s_me);
		return true;
	}
	time_t sendtime;
	time(&sendtime);
	do
	{
		if(do_receive() != -1)
		{
			mid = recv_buffer.read_ch();
			switch(mid)
			{
			case MID_HELLO_VERSION:
#ifdef BOTMSG
				cerr << "Server is running an incompatible version!" << endl;
#endif
				close(s_me);
				freeaddrinfo(servinfo);
				return true;
			case MID_HELLO_NEWID:
				cur_id = recv_buffer.read_sh();
				turn_ms = recv_buffer.read_sh();
				return false; // Connected!
			default: break;
			}
		} // received something
		usleep(MSG_DELAY_MS);
	} while(time(NULL) - sendtime < 2);
#ifdef BOTMSG
	cerr << "Did not get a reply from the server." << endl;
#endif
	close(s_me);
	freeaddrinfo(servinfo);
	return true;
}

/// AI related:
////////////////////////////////////////////

bool green_pc_color(const unsigned char c)
{
	return c == C_GREEN_PC || c == C_GREEN_PC_LIT || c == C_GREEN_ON_HEAL
		|| c == C_GREEN_ON_TRAP || c == C_GREEN_ON_POIS || c == C_GREEN_ON_DISG
	 	|| c == C_GREEN_ON_HIT || c == C_GREEN_ON_MISS;
}
bool purple_pc_color(const unsigned char c)
{
	return c == C_PURPLE_PC || c == C_PURPLE_PC_LIT || c == C_PURPLE_ON_HEAL
		|| c == C_PURPLE_ON_TRAP || c == C_PURPLE_ON_POIS || c == C_PURPLE_ON_DISG
	 	|| c == C_PURPLE_ON_HIT || c == C_PURPLE_ON_MISS;
}
bool brown_pc_color(const unsigned char c)
{
	return c == C_BROWN_PC || c == C_BROWN_PC_LIT || c == C_BROWN_ON_HEAL
		|| c == C_BROWN_ON_TRAP || c == C_BROWN_ON_POIS || c == C_BROWN_ON_DISG
	 	|| c == C_BROWN_ON_HIT || c == C_BROWN_ON_MISS;
}
bool light_trap_color(const unsigned char c)
{
	return c == C_LIGHT_TRAP || c == C_LIGHT_TRAP_LIT || c == C_YELLOW_ON_HEAL
		|| c == C_YELLOW_ON_TRAP || c == C_YELLOW_ON_POIS || c == C_YELLOW_ON_DISG
	 	|| c == C_YELLOW_ON_HIT || c == C_YELLOW_ON_MISS;
}


enum { WALK_DONT=0, WALK_OKAY, WALK_GOOD, WALK_GREAT };

// Estimate how wise it is to take a step in the given direction.
char score_walk(const e_Dir d, const bool avoid_pcs)
{
	// Figure out what there is where we're thinking of walking:
	Coords c = center.in(d);
	// colour is at (y*SIZE+x)*2, symbol at that+1
	char sym = viewbuffer[(c.y*VIEWSIZE+c.x)*2+1];

	if(sym == '?') // this means we are blind, walk seldom
	{
		if(random()%WALK_BLIND_CHANCE_1IN)
			return WALK_DONT;
		return WALK_GREAT; /* this optimises a bit; if we are blind,
			all the view is going to be full of '?'s anyway */
	}
	// Not blind:
	// Water only when must:
	if(sym == '~')
		return WALK_OKAY;
	// Check for things we absolutely don't want to walk into:
	if(sym == ' ' || sym == '#' // always to be avoided
		|| sym == '|' || sym == '-' // windows are like walls
		|| (sym == 'O' && !classes[myclass].can_push) // a boulder you can't push is like a wall
		|| (avoid_pcs && sym == '@')) // and PCs if requested
		return WALK_DONT;
	
	char col = viewbuffer[(c.y*VIEWSIZE+c.x)*2];

	// Assassins prefer nonlit tiles by walls; trappers prefer nonlit tree squares:
	if(myclass == C_ASSASSIN)
	{
		if(sym != '+' && col < C_TREE_LIT) // no door, not lit
			for(char ch = 0; ch < MAX_D; ++ch)
			{
				tmp_coords = c.in(e_Dir(ch));
				if(viewbuffer[(tmp_coords.y*VIEWSIZE+tmp_coords.x)*2+1] == '#')
					return WALK_GREAT; // by a wall
			}
		// give high score to light traps as they can be useful:
		if(sym == '^' && light_trap_color(col))
			return WALK_GREAT;
	}
	else if(myclass == C_TRAPPER && col == C_TREE)
		return WALK_GREAT;
	// Give lower score for rough and marsh (except with scouts and trappers),
	// as well as for traps that might be accidentally triggered
	if(((sym == '\"' || sym == ';') && myclass != C_TRAPPER && myclass != C_SCOUT)
		|| (sym == '^' && col < C_NEUT_FLAG))
		return WALK_OKAY;
	// We don't know if a boulder is pushable or not, so don't always try
	if(sym == 'O')
		return random()%2 ? WALK_OKAY : WALK_DONT;
	return WALK_GOOD; // just a walkable tile (this includes doors)
}

// Walk to direction d if can. If cannot, try ++d and --d in random order.
// Returns whether did move.
bool random_turn_from_dir(e_Dir d, const bool avoid_pcs, const bool ign_terrain)
{
	char score_d = score_walk(d, avoid_pcs);
	// if ignoring terrain, don't care about rating, just go there if possible:
	if(ign_terrain && score_d != WALK_DONT)
	{
		send_action(XN_MOVE, (prev_committed_walk = d));
		return true;
	}
	char score_pd = score_walk(++d, avoid_pcs);
	char score_md = score_walk(--(--d), avoid_pcs);

	if(score_d >= max(score_pd, score_md)) // d has best score
	{
		if(!score_d) // this means all scores == 0
			return false;
		// else:
		// note that ++d returns d to original value
		send_action(XN_MOVE, (prev_committed_walk = ++d));
	}
	else if(score_pd > score_md) // ++d has best score
		send_action(XN_MOVE, (prev_committed_walk = ++(++d)));
	else // --d has best score
		send_action(XN_MOVE, (prev_committed_walk = d));
	return true;
}

void random_walk()
{
	if(symbol_under_feet != '~' && !(random()%WAIT_ON_RANDOM_WALK_1IN))
		return;
	// a small chance of turning without reason:
	if(!(random()%TURN_WITHOUT_REASON_1IN))
	{
		if(random()%2) ++prev_committed_walk;
		else --prev_committed_walk;
	}
	// if can, continue in previous direction (which the above might have just turned)
	if(!random_turn_from_dir(prev_committed_walk, true, false))
	{
		// Couldn't; walk entirely randomly, then.
		e_Dir walkdir = e_Dir(random()%MAX_D);
		for(char i = 0; i < MAX_D; ++i)
		{
			if(score_walk(walkdir, true) != WALK_DONT)
			{
				send_action(XN_MOVE, (prev_committed_walk = walkdir));
				break; // pick first direction in which we *can* walk
			}
			++walkdir;
		}
		// if here, can't walk anywhere!
	}
}

void try_walk_towards(const Coords &c, const bool avoid_pcs)
{
	// if cannot walk towards c (trying 3 different steps), walk randomly.
	if(!random_turn_from_dir(center.dir_of(c), avoid_pcs, true))
		random_walk();
}

void scan_view() // extract PCs and flag in view, count walls
{
	pcs[0].clear();
	pcs[1].clear();
	seen_flag_owner = T_SPEC;
	num_walls_in_sight = 0;
	for(int i = 1; i < VIEWSIZE*VIEWSIZE*2; i += 2)
	{
		// get all PCs, not including self (center of view (note that VIEWSIZE
		// is odd!))
		if(viewbuffer[i] == '@')
		{
			if(i != (int(VIEWSIZE/2)*VIEWSIZE+int(VIEWSIZE/2))*2+1)
			{
				if(green_pc_color(viewbuffer[i-1]))
					pcs[0].push_back(Coords(((i-1)/2)%VIEWSIZE, ((i-1)/2)/VIEWSIZE));
				else if(purple_pc_color(viewbuffer[i-1]))
					pcs[1].push_back(Coords(((i-1)/2)%VIEWSIZE, ((i-1)/2)/VIEWSIZE));
				else if(brown_pc_color(viewbuffer[i-1]))
					pcs[myteam].push_back(Coords(((i-1)/2)%VIEWSIZE, ((i-1)/2)/VIEWSIZE));
			}
			continue;
		}
		if(viewbuffer[i] == '&') // flag
		{
			/* Note that since '@' etc. overrule '&' in the display, we don't
			 * have to check all possible colours (as they are, in fact, impossible) */
			switch(viewbuffer[i-1])
			{
			case C_GREEN_PC: case C_GREEN_PC_LIT: case C_GREEN_ON_MISS:
			case C_GREEN_ON_TRAP:
				seen_flag_owner = T_GREEN;
				break;
			case C_PURPLE_PC: case C_PURPLE_PC_LIT: case C_PURPLE_ON_MISS:
			case C_PURPLE_ON_TRAP:
				seen_flag_owner = T_PURPLE;
				break;
			case C_NEUT_FLAG: case C_NEUT_FLAG_LIT: case C_GRAY_ON_MISS:
			case C_GRAY_ON_TRAP:
				seen_flag_owner = T_NO_TEAM;
				break;
			}
			seen_flag_coords.x = ((i-1)/2)%VIEWSIZE;
			seen_flag_coords.y = ((i-1)/2)/VIEWSIZE;
		}
		// count number of walls (in LOS) for miners:
		else if(viewbuffer[i] == '#' && viewbuffer[i-1] >= C_WALL)
			++num_walls_in_sight;
	}
}

// Check if PC at given coords has class 'name' (abbreviation)
char last_check_class[3] = "";
void get_class_of(const Coords& c)
{
	// Titles are in the form [num of titles (char)][so many titles: [x][y][C-string]]
	char *p = viewbuffer+(VIEWSIZE*VIEWSIZE*2);
	for(char i = *(p++); i > 0; --i)
	{
		if(*p == c.x && *(p+1) == c.y)
		{
			// friendly PC is "nick|Cl", hostile "Cl"; this will
			// get the Cl-part only:
			p += strlen(p+2); // -2 for "Cl", but +2 to skip coords
			strncpy(last_check_class, p, 3);
			return;
		}
		p += 2;
		p += strlen(p);
	}
	last_check_class[0] = '\0';
}


// Return true if 'c' is the colour of the enemy team
bool enemy_team_col(const unsigned char c)
{
	if(myteam == T_GREEN)
		return purple_pc_color(c);
	// else myteam == T_PURPLE
	return green_pc_color(c);
}


// For searching teammates/enemies who are in a neighbouring square:
e_Dir neighb_pc(const char t)
{
	if(!pcs[t].empty())
	{
		for(char d = 0; d < MAX_D; ++d)
		{
			if(find(pcs[t].begin(), pcs[t].end(), center.in(e_Dir(d))) != pcs[t].end())
				return e_Dir(d);
		}
	}
	return MAX_D;
}


// Returns true if a shootable target is found and puts the coordinates in 'target'.
// Set 'cardinals' true to check only cardinal directions (for zaps).
bool could_shoot(Coords &target, const bool cardinals)
{
	// Go through enemies in sight:
	vector<Coords>::const_iterator eti;
	char r, line, ind;
	for(eti = pcs[opp_team[myteam]].begin(); eti != pcs[opp_team[myteam]].end(); ++eti)
	{
		if((r = center.dist_walk(*eti)) == 1) // enemy right next to us!
		{
			target = *eti;
			return true;
		}
		if(cardinals && !(line = abs(eti->x - center.x))
			&& !(ind = abs(eti->y - center.y)) && line != ind)
			continue; // this enemy not in a cardinal direction
		// Check that there are no teammates on the line of fire:
		// NOTE: atm not checking for walls etc...
		if(eti->y - VIEWSIZE/2 == -r) line = eti->x - VIEWSIZE/2 + r;
		else if(eti->x - VIEWSIZE/2 == r) line = 3*r + eti->y - VIEWSIZE/2;
		else if(eti->y - VIEWSIZE/2 == r) line = 5*r - eti->x + VIEWSIZE/2;
		else /* eti->x - VIEWSIZE/2 == -r */ line = 7*r - eti->y + VIEWSIZE/2;
		for(ind = 0; ind < 2*r; ind += 2)
		{
			if(find(pcs[myteam].begin(), pcs[myteam].end(),
				Coords(loslookup[r-2][line*2*r+ind] + VIEWSIZE/2,
				loslookup[r-2][line*2*r+ind+1] + VIEWSIZE/2))
				!= pcs[myteam].end())
				break;
		}
		if(ind == 2*r) // no teammates broke the loop:
		{
			target = *eti;
			return true;
		}
	}
	return false; // no targets found
}


bool could_capture_flag()
{
	// Always capture neutral flags, never "capture" own flags:
	if(seen_flag_owner == T_NO_TEAM)
		return true;
	if(seen_flag_owner == myteam || seen_flag_owner == T_SPEC)
		return false;
	// Regarding enemy flags, it's more complicated, and depends on game mode.
	// In Dominion, can (and should) always capture enemy flags. In Conquest
	// this is true for the green team.
	if(gm_dom1_con0 == 1 || (gm_dom1_con0 == 0 && myteam == T_GREEN))
		return true;
	// In other modes the enemy flag can be captured as long as it isn't their
	// last flag.
	return (num_enemy_flags > 1);
}

bool get_sound_to_follow(Coords &t)
{
	for(int i = 1; i < VIEWSIZE*VIEWSIZE*2; i += 2)
	{
		// Get first sound effect, not including center of view.
		// Always follow voices, other sounds only half the time.
		if(viewbuffer[i] == '!' && i != VIEWSIZE*(VIEWSIZE+1)+1
			&& (viewbuffer[i-1] == VOICE_COLOUR || random()%2))
		{
			t.x = ((i-1)/2)%VIEWSIZE;
			t.y = ((i-1)/2)/VIEWSIZE;
			// do not follow very nearby sounds (dependent on LOS radius)
			if(center.dist_walk(t) >= classes[myclass].losr)
				return true;
		}
	}
	return false;
}

bool teammate_to_follow(Coords &t)
{
	// Scouts and trappers do not follow anyone
	if(myclass == C_SCOUT || myclass == C_TRAPPER)
		return false;
	// Go through seen teammates:
	vector<Coords>::const_iterator ti;
	for(ti = pcs[myteam].begin(); ti != pcs[myteam].end(); ++ti)
	{
		get_class_of(*ti);
		// Follow first found Scout or Miner
		if(!strcmp(last_check_class, "Sc")
			|| !strcmp(last_check_class, "Mi"))
		{
			t = *ti;
			return true;
		}
	}
	// No scouts or miners; follow anyone with a small chance:
	if(!pcs[myteam].empty() && !(random()%FOLLOW_ANYONE_1IN))
	{
		t = pcs[myteam][randor0(pcs[myteam].size())];
		return true;
	}
	return false;
}

bool get_enemy_corpse_dir(Coords &t)
{
	// scout and not already disguised:
	if(myclass == C_SCOUT && !brown_pc_color(viewbuffer[(center.y*VIEWSIZE+center.x)*2]))
	{
		for(int i = 1; i < VIEWSIZE*VIEWSIZE*2; i += 2)
		{
			if(viewbuffer[i] == '%' && enemy_team_col(viewbuffer[i-1]))
			{
				t.x = ((i-1)/2)%VIEWSIZE;
				t.y = ((i-1)/2)/VIEWSIZE;
				return true;
			}
		}
	}
	return false;
}

/* Functions for each class to check for using the class's special ability */

bool no_class_spc_Ar()
{
	if(could_shoot(tmp_coords, false))
	{
		/* Shoot if had a clear shot for some turns, otherwise keep "aiming"
		 * (do nothing). Note that having a clear shot to different targets at
		 * different turns counts... */
		if(++abil_counter == AIM_TURNS)
		{
			send_action(XN_SHOOT, tmp_coords.x - VIEWSIZE/2, tmp_coords.y - VIEWSIZE/2);
			abil_counter = 0;
		} // else still taking the aim
		else if(center.dist_walk(tmp_coords) == 1)
		{
			// stop aiming if enemy is right there; hit instead!
			abil_counter = 0;
			return true; // may take other action this turn
		}
		return false; // even if didn't do anything
	}// else:
	if(abil_counter)
		--abil_counter; // gradually lower aim
	return true;
}

bool no_class_spc_As()
{
	// We can either use a flash bomb or trigger a light trap. First generic
	// checks:
	if(limiter)
		return true;
	// Need at least one non-assassin enemy and no non-assassin friends
	// within range (3 tiles; cf. server/chores.cpp:flash_at())
	vector<Coords>::const_iterator ti;
	for(ti = pcs[myteam].begin(); ti != pcs[myteam].end(); ++ti)
	{
		if(center.dist_walk(*ti) <= 3)
		{
			get_class_of(*ti);
			if(strcmp(last_check_class, "As")) // not assassin
				return true;
		}
	}
	for(ti = pcs[opp_team[myteam]].begin(); ti != pcs[opp_team[myteam]].end(); ++ti)
	{
		if(center.dist_walk(*ti) <= 3)
		{
			get_class_of(*ti);
			if(strcmp(last_check_class, "As")) // not assassin
				break;
		}
	}
	if(ti == pcs[opp_team[myteam]].end())
		return true;
	// Now check to use a light trap:
	if(symbol_under_feet == '^' && light_trap_color(col_under_feet))
	{
		send_action(XN_TRAP_TRIGGER);
		limiter = FLASH_LIMIT;
		return false;
	}
	// No trap chance, check to use a bomb:
	if(!abil_counter)
		return true;
	send_action(XN_FLASH);
	--abil_counter;
	limiter = FLASH_LIMIT;
	return false;
}

bool no_class_spc_Cm()
{
	if(!limiter && could_shoot(tmp_coords, true))
	{
		send_action(XN_ZAP, center.dir_of(tmp_coords));
		wait_turns = ZAP_WAIT;
		limiter = ZAP_LIMIT;
		return false;
	}
	return true;
}

bool no_class_spc_Mc()
{
	if(myhp <= 2*classes[C_MINDCRAFTER].hp/3
		&& neighb_pc(opp_team[myteam]) != MAX_D)
	{
		send_action(XN_BLINK); // jump away from combat
		wait_turns = BLINK_WAIT;
		return false;
	}
	return true;
}

bool no_class_spc_Sc()
{
	/* Must stand on an enemy corpse, not be already disguises, and not have
	 * enemies in sight: */
	if(symbol_under_feet == '%' && pcs[opp_team[myteam]].empty()
		&& !brown_pc_color(viewbuffer[(center.y*VIEWSIZE+center.x)*2])
		&& enemy_team_col(col_under_feet))
	{
		send_action(XN_DISGUISE);
		wait_turns = DISGUISE_WAIT;
		return false;
	}
	return true;
}

bool no_class_spc_Fi()
{
	if(neighb_pc(myteam) != MAX_D)
		return true;
	if((rv = neighb_pc(opp_team[myteam])) != MAX_D)
	{
		send_action(XN_CIRCLE_ATTACK, rv); // do circle attack
		wait_turns = CIRCLE_ATTACK_WAIT;
		return false;
	}
	return true;
}

bool no_class_spc_Mi()
{
	// If there are enemies in view, don't dig. Also, the chance to dig in
	// general is higher when there are more walls in sight.
	if(!pcs[opp_team[myteam]].empty()
		|| random()%WALLS_FOR_CERTAIN_DIG >= num_walls_in_sight)
		return true;
	// else; primarily dig in the direction of previous move:
	e_Dir d = prev_committed_walk;
	for(rv = 0; rv < MAX_D; ++rv)
	{
	 	tmp_coords = center.in(d);
		if(viewbuffer[(tmp_coords.y*VIEWSIZE+tmp_coords.x)*2+1] == '#')
			break;
		++d;
	}
	if(rv < MAX_D)
	{
		// set mining dir as last move dir; this way miners are more likely to
		// continue digging roughly in the same direction (?)
		send_action(XN_MINE, (prev_committed_walk = d));
		wait_turns = DIG_WAIT;
		return false;
	}
	return true;
}

bool no_class_spc_He()
{
	if(!limiter)
	{
		if((rv = neighb_pc(opp_team[myteam])) != MAX_D
			|| (rv = neighb_pc(myteam)) != MAX_D)
		{
			send_action(XN_HEAL, rv); // poison enemy or heal teammate
			limiter = HEAL_POISON_LIMIT; // don't heal again for this many turns
			return false;
		}
		/*else*/ if(myhp <= 2*classes[C_HEALER].hp/3)
		{
			send_action(XN_HEAL, MAX_D); // heal self
			limiter = HEAL_SELF_LIMIT;
			return false;
		}
	}
	return true;
}

bool no_class_spc_Wi()
{
	// if no enemies and torch not lit, light it:
	if(pcs[opp_team[myteam]].empty())
	{
		if(!torchlit)
		{
			send_action(XN_TORCH_HANDLE);
			torchlit = true; /* necessary, because a STATE_UPD might get delayed */
			return false;
		}
	}
	// at least one enemy, no friendlies, no adjacent enemies (melee is better)
	else if(pcs[myteam].empty() && neighb_pc(opp_team[myteam]) == MAX_D)
	{
		send_action(XN_MM); // cast magic missile
		wait_turns = CAST_MM_WAIT;
		return false;
	}
	return true;
}

bool no_class_spc_Tr()
{
	if(!limiter && pcs[opp_team[myteam]].empty())
	{
		send_action(XN_SET_TRAP);
		wait_turns = TRAP_WAIT;
		limiter = TRAP_LIMIT; // wait a good while before setting another one
		return false;
	}
	return true;
}

bool no_class_spc_Pw() { return true; }

// function pointers to the above functions
bool (*no_cl_spc[NO_CLASS])() = {
	no_class_spc_Ar, no_class_spc_As, no_class_spc_Cm, no_class_spc_Mc,
	no_class_spc_Sc, no_class_spc_Fi, no_class_spc_Mi, no_class_spc_He,
	no_class_spc_Wi, no_class_spc_Tr, no_class_spc_Pw };

// See if decides to commit a class-specific action. Returns false if yes.
bool no_class_specific()
{
	// To make bot behaviour a bit less predictable, there is, for each class,
	// a fixed chance that we simply don't even consider the class specific
	// action. No class should use ability in water.
	if(symbol_under_feet == '~' || random()%100 < CHANCE_IGN_CL_SPC[myclass])
		return true;
	// Otherwise check for possibility of using special abilities:
	return no_cl_spc[myclass]();
}

// Misc:
// /////////////////////

void init_rng()
{
	/* Doing the usual "srandom(time(NULL));" has a nasty side effect: bots
	 * spawned at the same time usually spawn at the same second, so they
	 * have the same random seed. Instead, seed from dev/urandom: */
	ifstream urf("/dev/urandom", ios_base::binary);
	if(!urf)
	{
#ifdef BOTMSG
		cerr << "No /dev/urandom access to seed rng!" << endl;
#endif
		srandom(time(NULL)); // settle with this
	}
	else
	{
		urf.read(reinterpret_cast<char*>(&rv), sizeof(int));
		srandom(rv);
	}
}


// MAIN:
// ///////////////////////////////

int main(int argc, char *argv[])
{
	string sip;
	if(argc > 1)
	{
		if((sip = argv[1]) == "-v")
		{
			cout << "mrbrown (" PACKAGE " bot) v. " << VERSION << endl;
			return 0;
		}
	}
	else sip =
#ifdef BOT_IPV6
		"[::1]:12360";
#else
		"127.0.0.1:12360";
#endif

	if(do_connect(sip))
		return 1;

	init_rng();

	// Send spawn request: (no sense having spectator bots!)
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_SPAWN_AXN);
	send_buffer.add(cur_id);
	/* Don't intentionally become scout/planewalker, since we don't have
	 * any real AI rules for their special abilities. Note that bots might still,
	 * by classlimiting, be spawned as these classes (myclass will ultimately
	 * be what the server replies). */
	do myclass = e_Class(random()%NO_CLASS);
	while(myclass == C_PLANEWALKER);
	send_buffer.add((unsigned char)myclass);
	do_send();

	short msglen;
	vector<Coords>::const_iterator picked, ci;
	for(;;)
	{
		if((msglen = do_receive()) != -1)
		{
			if((mid = recv_buffer.read_ch()) == MID_QUIT)
				break;
			if(mid == MID_VIEW_UPD)
			{
				curturn = recv_buffer.read_sh();
				axn_counter = 0;
				// Return a "pong":
				send_buffer.clear();
				send_buffer.add((unsigned char)MID_VIEW_UPD);
				send_buffer.add(cur_id);
				send_buffer.add(curturn);
				do_send();

				if(msglen > 5 // msg is not very small => view has changed
					&& myhp > 0) // do not scan view when dead
				{
					recv_buffer.read_compressed(viewbuffer);
					// NOTE: not checking for errors -- might get a faulty view.
					scan_view();
				}
			}
			else if(mid == MID_ADD_MSG)
			{
				recv_buffer.read_ch(); // cpair
#ifdef BOTMSG
				string msg_str;
				for(mid = recv_buffer.read_ch(); mid > 0; --mid)
				{
					recv_buffer.read_str(msg_str);
					if(msg_str == "That is no real wall!")
						wait_turns = 0; // stop trying to dig illusory wall!
					cout << "MSG: " << msg_str << endl;
				}
#else
			if(recv_buffer.read_ch() == 1
				&& !strcmp(recv_buffer.getr()+3, "That is no real wall!"))
				wait_turns = 0;	// stop trying to dig illusory wall!
#endif
			}
#ifdef BOTMSG
			// If bot messaging is enables, we print out (chat) messages.
			else if(mid == MID_SAY_CHAT)
			{
				recv_buffer.read_sh(); // skip id
				string line;
				recv_buffer.read_str(line);
				string speaker_nick;
				recv_buffer.read_str(speaker_nick);
				cout << speaker_nick << " SAYS: " << line << endl;
			}
#endif
			else if(mid == MID_STATE_UPD)
			{
				rv = myhp; // store old hp
				myhp = static_cast<char>(recv_buffer.read_ch());
				if(torchlit = (rv <= 0 && myhp > 0)) /* this means we spawned
					* (use torchlit as a temp variable) */
				{
					// wizards can immediately light their torch:
					if(myclass == C_WIZARD)
						send_action(XN_TORCH_HANDLE);
					/* Everyone ought to wait so we don't act based on an old
					 * view (VIEW_UPD might arrive after STATE_UPD on this
					 * turn). So wizards light their torch and others just stand
					 * around for a single turn. */
					wait_turns = 1;
					isolation_turns = random()%50;
				}
				else if(myhp <= 0 && rv > 0) // this means we died
					symbol_under_feet = col_under_feet = 0;
				// TODO: use more of these?
				recv_buffer.read_ch(); // tohit
				recv_buffer.read_ch(); // +damage
				recv_buffer.read_ch(); // dv
				recv_buffer.read_ch(); // poisoned (0/1)
				rv = recv_buffer.read_ch(); // sector
				if(torchlit) /* this means we spawned! (See above.)
					* Set walking dir towards center, or to random if spawning
					* in central sector. */
					prev_committed_walk = (rv == MAX_D) ? e_Dir(random()%MAX_D)
						: !e_Dir(rv);
				rv = recv_buffer.read_ch(); // torch symbol
				torchlit = (rv != ' ' && rv != ','); /* NOTE: if we don't have a torch,
					we say the torch is 'lit', so that we won't try to light it. */
			}
			else if(mid == MID_STATE_CHANGE)
			{
				if((myclass = e_Class(recv_buffer.read_ch())) == NO_CLASS)
					myhp = 0; // was forced into a spectator
				else if(myclass == C_ARCHER)
					abil_counter = 0;
				else if(myclass == C_ASSASSIN)
					abil_counter = 3; // flash bombs
				myteam = e_Team(recv_buffer.read_ch());
				limiter = 0;
			}
			else if(mid == MID_GAME_UPD)
			{
				// Extract game mode.
				recv_buffer.read_ch(); // number of green players
				recv_buffer.read_ch(); // number of purple players
				/* Instead of reading the entire game status string, we can
				 * deduce the game mode from the first two letters. We are only
				 * interested in Dominion and Conquest. */
				if((mid = recv_buffer.read_ch()) == 'C')
					gm_dom1_con0 = 0;
				else if(mid == 'D' && recv_buffer.read_ch() == 'o')
					gm_dom1_con0 = 1;
				else
					gm_dom1_con0 = -1; // neither dominion nor conquest
			}
			else if(mid == MID_FLAG_UPD)
			{
				// We are only interested in the number of enemy flags, and that
				// only if the game mode is neither dominion or conquest:
				if(gm_dom1_con0 == -1)
				{
					num_enemy_flags = 0;
					for(rv = 0; rv <= MAX_D; ++rv)
					{
						mid = recv_buffer.read_ch();
						if((myteam == T_GREEN && (mid == C_PURPLE_PC || mid == C_PURPLE_PC_LIT))
						|| (myteam == T_PURPLE && (mid == C_GREEN_PC || mid == C_GREEN_PC_LIT)))
							++num_enemy_flags;
					}
				}
			}
#if 0
			// Maybe do something with this sometime?
			else if(mid == MID_TIME_UPD) { }
#endif
			continue; // as long as we receive messages, keep handling them!
		} // receiving messages
		else usleep(MSG_DELAY_MS);
		
		if(myhp > 0 && reftimer.update() - last_sent_axn > turn_ms)
		{
			if(wait_turns) // something has forced us to wait
				--wait_turns;
			else
			{
				if(limiter)
					--limiter;
				// Basic decision making:
				// 1. Check if could/should use class ability
				// 2. If not, move (which can mean a lot of things,
				// including melee)
				if(no_class_specific()) // not doing something special
				{
					// Other actions (mostly walking).
					// Check if there are enemies in sight:
					if(pcs[opp_team[myteam]].empty()) // (no)
					{
						/* Check to light own torch (if happen to stand on a
						 * static one): */
						if(myclass != C_WIZARD && !torchlit
							/*&& classes[myclass].torch*/ && symbol_under_feet == 't')
						{
							send_action(XN_TORCH_HANDLE);
							torchlit = true;
						}
						/* Walk primarily towards any seen flags we could
						 * capture, secondarily scouts towards enemy corpses,
						 * then follow teammates (see separate logic),
						 * then towards heard sounds, lastly randomly.
						 * Don't walk on PCs in the "try_walk_towards" calls. */
						else if(could_capture_flag())
						{
							try_walk_towards(seen_flag_coords, true);
							isolation_turns = random()%50;
						}
						else if(get_enemy_corpse_dir(tmp_coords))
							try_walk_towards(tmp_coords, true);
						else if(get_sound_to_follow(tmp_coords)) // have sound to follow
							try_walk_towards(tmp_coords, true);
						// No enemies, flags, or sounds; we might be isolated:
						else if(++isolation_turns
							>= ISOLATION_TO_SUICIDE)
						{
							isolation_turns = random()%50;
							send_action(XN_SUICIDE);
						}
						// Ignore following teammates every third time:
						else if(random()%3 && teammate_to_follow(tmp_coords))
							try_walk_towards(tmp_coords, true);
						else
							random_walk();
					}
					else // there are enemies in sight
					{
						rv = VIEWSIZE; // find the closest one
						for(ci = pcs[opp_team[myteam]].begin(); ci != pcs[opp_team[myteam]].end(); ++ci)
						{
							if(ci->dist_walk(center) < rv)
							{
								rv = ci->dist_walk(center);
								picked = ci;
							}
						}
						/* if the closest one is not very close and playing a
						 * scout, do nothing (spotting the enemy for teammates) */
						if(rv < 6 || myclass != C_SCOUT)
							try_walk_towards(*picked, false); // *do* walk on PCs
						isolation_turns = random()%50;
					}
				} // walking
			} // not randomly waiting
			last_sent_axn.update(); // behave as if acted even if didn't
		} // alive, and enough time passed to take next action
	} // for eva (until server sends QUIT)

	// Disconnect
#ifdef BOTMSG
	cout << "Server closed." << endl;
#endif
	freeaddrinfo(servinfo);
	close(s_me);
	return 0;
}


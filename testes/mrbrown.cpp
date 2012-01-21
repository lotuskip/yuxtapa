// Please see LICENSE file.
#include "../common/netutils.h"
#include "../common/timer.h"
#include "../common/classes_common.h"
#include "../common/coords.h"
#include "../common/col_codes.h"
#include "../common/los_lookup.h"
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
#ifdef BOTMSG
#include <iostream>
#endif

using namespace std;

// CONSTANTS
/////////////
const Coords center(VIEWSIZE/2, VIEWSIZE/2);
// a CHANCE is a chance of something happening. Usually these are
// "1IN" chances, meaning that there is a "1 in x" chance of
// something happening.
const char CHANCE_IGN_CL_SPC[NO_CLASS] = {
	// chances, in %, of not even trying to use special ability
	10 /*Ar*/, 10 /*As*/, 15 /*Cm*/, 33 /*Mc*/, 100 /*Sc*/, 15 /*Fi*/,
	15 /*Mi*/, 10 /*He*/, 10 /*Wi*/, 30 /*Tr*/, 100 /*Pw*/
};
const char TURN_CHANCE_1IN = 22; // turning randomly without reason
const char STAND_CHANCE_1IN = 8; // doing nothing when otherwise would walk
const char WALK_BLIND_CHANCE_1IN = 7; // walking around blind
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
// Other stuff:
const char AIM_TURNS = 4; // how many turns must an archer take aim before firing
const char WALLS_FOR_CERTAIN_DIG = 38; // how many walls in sight ensure that miners will dig when possible
const int MSG_DELAY_MS = 10000; // wait for 10ms between checking for server messages


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
// AI related:
e_Dir prev_committed_walk; // direction of last step taken
vector<Coords> pcs[2]; // PCs of each team currently in view
char limiter = 0; // used to prevent bots from repeating the same action too often
char abil_counter = 0; // used for various ability-related counting needs
Coords seen_flag_coords; // if a flag has been spotted
e_Team seen_flag_owner = T_SPEC;
unsigned char num_walls_in_sight = 0;
// Generic:
Coords tmp_coords;
int rv; // "return value"; although used for other things, too

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
		if(sip.rfind(':', i-1) != string::npos) // two ':'s imply IPv6 (or garbage...)
			--i; // there is "::", not just ":" before the port
		ip = sip.substr(0, i);
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
	// Check for things we absolutely don't want to walk into:
	if(sym == ' ' || sym == '~' || sym == '#' // always to be avoided
		|| sym == '|' || sym == '-' // windows are like walls
		|| (sym == 'O' && !classes[myclass].can_push) // a boulder you can't push is like a wall
		|| (avoid_pcs && sym == '@')) // and PCs if requested
		return WALK_DONT;
	
	char col = viewbuffer[(c.y*VIEWSIZE+c.x)*2];

	// Assassins prefer nonlit tiles by walls; trappers prefer nonlit tree squares:
	if(myclass == C_ASSASSIN && sym != '+' && col < C_TREE_LIT) // no door, not lit
	{
		for(char ch = 0; ch < MAX_D; ++ch)
		{
			tmp_coords = c.in(e_Dir(ch));
			if(viewbuffer[(tmp_coords.y*VIEWSIZE+tmp_coords.x)*2+1] == '#')
				return WALK_GREAT; // by a wall
		}
	}
	else if(myclass == C_TRAPPER && col == C_TREE)
		return WALK_GREAT;
	// Give lower score for rough and marsh (except with scouts and trappers),
	// as well as for traps that might be accidentally triggered
	if(((sym == '\"' || sym == ';') && myclass != C_TRAPPER && myclass != C_SCOUT)
		|| (sym == '^' && col < C_NEUT_FLAG))
		return WALK_OKAY;
	// We cannot know if a boulder is pushable or not, so don't always try
	if(sym == 'O')
		return random()%2 ? WALK_OKAY : WALK_DONT;
	return WALK_GOOD; // just a walkable tile (this includes doors)
}

// Walk to direction d if can. If cannot, try ++d and --d in random order.
// Returns whether did move.
bool random_turn_from_dir(e_Dir d, const bool avoid_pcs)
{
	char score_d = score_walk(d, avoid_pcs);
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
	// a small chance of turning without a reason:
	if(!(random()%TURN_CHANCE_1IN))
	{
		if(random()%2) ++prev_committed_walk;
		else --prev_committed_walk;
	}
	// if can, continue in previous direction (which the above might have just turned)
	if(!random_turn_from_dir(prev_committed_walk, true))
	{
		// walk entirely randomly, then.
		// A small chance to just stand still: (1 in 9)
		if(random()%STAND_CHANCE_1IN)
		{
			// Figure out a random dir to walk
			e_Dir walkdir = e_Dir(random()%MAX_D);
			e_Dir goodscorer = MAX_D;
			e_Dir onescorer = MAX_D;
			char i, j;
			for(i = 0; i < MAX_D; ++i)
			{
				if((j = score_walk(walkdir, true)) == WALK_GREAT)
					break; // pick direction with score > 1 immediately
				if(j == WALK_GOOD)
					goodscorer = walkdir;
				else if(j != WALK_DONT)
					onescorer = walkdir;
				++walkdir;
			}
			if(i < MAX_D) // found a very good direction
				send_action(XN_MOVE, (prev_committed_walk = walkdir));
			else if(goodscorer != MAX_D) // found a good direction
				send_action(XN_MOVE, (prev_committed_walk = goodscorer));
			else if(onescorer != MAX_D) // found an acceptable direction
				send_action(XN_MOVE, (prev_committed_walk = onescorer));
			// else can't walk anywhere!
		}
	}
}

void try_walk_towards(const Coords &c, const bool avoid_pcs)
{
	// if cannot walk towards c (trying 3 different steps), walk randomly.
	if(!random_turn_from_dir(center.dir_of(c), avoid_pcs))
		random_walk();
}

bool scan_view() // extract PCs in view and check if in closed area
{
	pcs[0].clear();
	pcs[1].clear();
	seen_flag_owner = T_SPEC;
	int num_oos // number of tiles out of sight
		= num_walls_in_sight = 0;
	for(int i = 1; i < VIEWSIZE*VIEWSIZE*2; i += 2)
	{
		// get all PCs, not including self (center of view (note that VIEWSIZE
		// is odd!))
		if(viewbuffer[i] == '@')
		{
			if(i != (int(VIEWSIZE/2)*VIEWSIZE+int(VIEWSIZE/2))*2+1)
			{
			switch(viewbuffer[i-1])
			{
			case C_GREEN_PC: case C_GREEN_PC_LIT: case C_GREEN_ON_HEAL:
			case C_GREEN_ON_TRAP: case C_GREEN_ON_POIS: case C_GREEN_ON_DISG:
			case C_GREEN_ON_HIT: case C_GREEN_ON_MISS:
				pcs[0].push_back(Coords(((i-1)/2)%VIEWSIZE, ((i-1)/2)/VIEWSIZE));
				break;
			case C_PURPLE_PC: case C_PURPLE_PC_LIT: case C_PURPLE_ON_HEAL:
			case C_PURPLE_ON_TRAP: case C_PURPLE_ON_POIS: case C_PURPLE_ON_DISG:
			case C_PURPLE_ON_HIT: case C_PURPLE_ON_MISS:
				pcs[1].push_back(Coords(((i-1)/2)%VIEWSIZE, ((i-1)/2)/VIEWSIZE));
				break;
			case C_BROWN_PC: case C_BROWN_PC_LIT: case C_BROWN_ON_HEAL:
			case C_BROWN_ON_TRAP: case C_BROWN_ON_POIS: case C_BROWN_ON_DISG:
			case C_BROWN_ON_HIT: case C_BROWN_ON_MISS:
				pcs[myteam].push_back(Coords(((i-1)/2)%VIEWSIZE, ((i-1)/2)/VIEWSIZE));
				break;
			}
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
				seen_flag_owner = T_GREEN;
				break;
			case C_PURPLE_PC: case C_PURPLE_PC_LIT: case C_PURPLE_ON_MISS:
				seen_flag_owner = T_PURPLE;
				break;
			case C_NEUT_FLAG: case C_NEUT_FLAG_LIT: case C_GRAY_ON_MISS:
				seen_flag_owner = T_NO_TEAM;
				break;
			}
			seen_flag_coords.x = ((i-1)/2)%VIEWSIZE;
			seen_flag_coords.y = ((i-1)/2)/VIEWSIZE;
		}
		// count number of walls (in LOS) for miners:
		else if(viewbuffer[i] == '#' && viewbuffer[i-1] >= C_WALL)
			++num_walls_in_sight;
		if(viewbuffer[i-1] <= C_WATER_DIM || viewbuffer[i-1] == C_UNKNOWN)
			++num_oos;
	}
	/* We are stuck if we can see no more than 12 tiles. The second condition
	 * here rules out being blind: */
	return num_oos >= VIEWSIZE*VIEWSIZE-12 && num_oos != VIEWSIZE*VIEWSIZE;
}

// Check if PC at given coords has class 'name' (abbreviation)
bool is_class_of(const Coords& c, const char* name)
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
			return !strcmp(p, name);
		}
		p += 2;
		p += strlen(p);
	}
	return false; // no suck coords titled
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
		// get first sound effect, not including center of view and rumbles,
		// because those are the far-too-common digging sounds
		if(viewbuffer[i] == '!' && i != VIEWSIZE*(VIEWSIZE+1)+1
			&& viewbuffer[i-1] != sound_col[S_RUMBLE])
		{
			t = Coords(((i-1)/2)%VIEWSIZE, ((i-1)/2)/VIEWSIZE);
			return true;
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
	if(!abil_counter || limiter)
		return true;
	// Need at least one non-assassin enemy and no non-assassin friends
	// within range (3 tiles; cf. server/chores.cpp:flash_at())
	vector<Coords>::const_iterator ti;
	for(ti = pcs[myteam].begin(); ti != pcs[myteam].end(); ++ti)
	{
		if(center.dist_walk(*ti) <= 3 && !is_class_of(*ti, "As"))
			return true;
	}
	for(ti = pcs[opp_team[myteam]].begin(); ti != pcs[opp_team[myteam]].end(); ++ti)
	{
		if(center.dist_walk(*ti) <= 3 && !is_class_of(*ti, "As"))
			break;
	}
	if(ti == pcs[opp_team[myteam]].end())
		return true;
	// okay to flash:
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

bool no_class_spc_Sc() { return true; }

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
	// else:
	e_Dir d = e_Dir(random()%MAX_D);
	for(rv = 0; rv < MAX_D; ++rv)
	{
	 	tmp_coords = center.in(d);
		if(viewbuffer[(tmp_coords.y*VIEWSIZE+tmp_coords.x)*2+1] == '#')
			break;
		++d;
	}
	if(rv < MAX_D)
	{
		send_action(XN_MINE, d);
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
	// at least one enemy, no friendlies, no adjacent enemies (melee is better)
	if(pcs[myteam].empty() && !pcs[opp_team[myteam]].empty()
		&& neighb_pc(opp_team[myteam]) == MAX_D)
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
	// action:
	if(rand()%100 < CHANCE_IGN_CL_SPC[myclass])
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
	else
		sip = "127.0.0.1:12360";

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
	while(myclass == C_SCOUT || myclass == C_PLANEWALKER);
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

				if(msglen > 5) // msg is not very small => view has changed
				{
					recv_buffer.read_compressed(viewbuffer);
					// NOTE: not checking for errors -- might get a faulty view.
					if(myhp > 0 && // don't use processor time to scan when dead
						scan_view())
						send_action(XN_SUICIDE);
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
				if(rv <= 0 && myhp > 0 // this means we spawned
					&& myclass == C_WIZARD)
				{
					// wizards can immediately light their torch:
					send_action(XN_TORCH_HANDLE);	
					wait_turns = 1;
				}
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
				prev_committed_walk = e_Dir(random()%MAX_D);
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
		} // received a msg
		else usleep(MSG_DELAY_MS);
		
		if(myhp > 0) // When alive, do sum'n
		{
			if(reftimer.update() - last_sent_axn > turn_ms)
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
						// Try walking.
						// Check if there are enemies in sight:
						if(pcs[opp_team[myteam]].empty()) // (no)
						{
							/* Walk primarily towards any seen flags we could
							 * capture, secondarily towards heard sounds (except
							 * mining), thirdly randomly. Don't walk on PCs
							 * in the "try_walk_towards" calls. */
							if(could_capture_flag())
								try_walk_towards(seen_flag_coords, true);
							else if(get_sound_to_follow(tmp_coords)) // have sound to follow
								try_walk_towards(tmp_coords, true);
							else
								random_walk();
						}
						else // there are enemies in sight
						{
							rv = VIEWSIZE; // find the closest one
							for(picked = ci = pcs[opp_team[myteam]].begin(); ci != pcs[opp_team[myteam]].end(); ++ci)
							{
								if(ci->dist_walk(center) < rv)
								{
									rv = ci->dist_walk(center);
									picked = ci;
								}
							}
							try_walk_towards(*picked, false); // *do* walk on PCs
						}
					} // walking
				} // no need to wait until previous action is done
				last_sent_axn.update(); // behave as if acted even if didn't
			} // enough time passed to take next action
		} // is alive
	} // for eva (until server sends QUIT)

	// Disconnect
#ifdef BOTMSG
	cout << "Server closed." << endl;
#endif
	freeaddrinfo(servinfo);
	close(s_me);
	return 0;
}


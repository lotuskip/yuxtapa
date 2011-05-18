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
#include <string>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#ifdef BOTMSG
#include <iostream>
#endif

using namespace std;

SerialBuffer recv_buffer, send_buffer;
int s_me; // socket
struct addrinfo *servinfo, *the_serv;
unsigned short cur_id;
unsigned short curturn = 0;
unsigned char axn_counter = 0;
char wait_turns = 0;
char myhp = 0;
e_Team myteam;
e_Class myclass = NO_CLASS;
char viewbuffer[BUFFER_SIZE];

/// Networking related:
////////////////////////

msTimer last_sent_axn, reftimer;

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

sockaddr them;
socklen_t addr_len;
short do_receive()
{
	addr_len = sizeof(them);
	return recvfrom(s_me, recv_buffer.getw(), BUFFER_SIZE, 0, &them, &addr_len);
}

/// AI related:
////////////////////////////////////////////

const Coords center(VIEWSIZE/2, VIEWSIZE/2);

enum { WALK_DONT=0, WALK_OKAY, WALK_GOOD, WALK_GREAT };

// Estimate how wise it is to take a step in the current direction.
char score_walk(const e_Dir d, const bool avoid_pcs)
{
	// Figure out what is where we're thinking of walking:
	Coords c = center.in(d);
	// colour is at (y*SIZE+x)*2, symbol at that+1
	char sym = viewbuffer[(c.y*VIEWSIZE+c.x)*2+1];

	// Check for things we absolutely don't want to walk into:
	if(sym == '?' // don't walk when blind
		|| sym == '~' || sym == '#' // always to be avoided
		|| (sym == 'O' && !classes[myclass].can_push) // a boulder you can't push is like a wall
		|| (avoid_pcs && sym == '@')) // and PCs if requested
		return WALK_DONT;
	
	// Assassins prefer nonlit tiles by walls; trappers prefer nonlit tree squares:
	if(myclass == C_ASSASSIN)
	{
		if(sym != '+' && viewbuffer[(c.y*VIEWSIZE+c.x)*2] < C_TREE_LIT) // no door, not lit
		{
			Coords cn;
			for(char ch = 0; ch < MAX_D; ++ch)
			{
				cn = c.in(e_Dir(ch));
				if(viewbuffer[(cn.y*VIEWSIZE+cn.x)*2+1] == '#')
					return WALK_GREAT; // by a wall
			}
		}
	}
	else if(myclass == C_TRAPPER)
	{
		if(viewbuffer[(c.y*VIEWSIZE+c.x)*2] == C_TREE)
			return WALK_GREAT;
	}
	// Give lower score for rough and marsh (except with scouts and trappers),
	// as well as for pushing boulders (myclass is one that can push, by the above),
	// as well as for traps that might be accidentally triggered
	if(sym == 'O'
		|| ((sym == '\"' || sym == ';') && myclass != C_TRAPPER && myclass != C_SCOUT)
		|| (sym == '^' && viewbuffer[(c.y*VIEWSIZE+c.x)*2] < C_NEUT_FLAG))

		return WALK_OKAY;
	return WALK_GOOD; // just a walkable tile (this includes doors)
}

e_Dir prev_committed_walk = MAX_D; // direction of last step taken

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
	// if can, continue in previous direction
	if(!random_turn_from_dir(prev_committed_walk, true))
	{
		// walk entirely randomly, then.
		// A small chance to just stand still: (1 in 9)
		if(random()%9)
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

vector<Coords> pcs[2];
void extract_pcs()
{
	pcs[0].clear();
	pcs[1].clear();
	for(int i = 1; i < VIEWSIZE*VIEWSIZE*2; i += 2)
	{
		// get all PCs, not including self (center of view)
		if(viewbuffer[i] == '@' && i != VIEWSIZE*(VIEWSIZE+1)+1)
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
			}
			// TODO: brown PCs?
		}
	}
}

// Returns direction of first neighbouring teammate if any (MAX_D if not found)
e_Dir neighb_teammate()
{
	if(!pcs[myteam].empty())
	{
		for(char d = 0; d < MAX_D; ++d)
		{
			if(find(pcs[myteam].begin(), pcs[myteam].end(), center.in(e_Dir(d))) != pcs[myteam].end())
				return e_Dir(d);
		}
	}
	return MAX_D;
}

// Returns direction of first neighbouring enemy if any (MAX_D if not found)
e_Dir neighb_enemy()
{
	if(!pcs[opp_team[myteam]].empty())
	{
		for(char d = 0; d < MAX_D; ++d)
		{
			if(find(pcs[opp_team[myteam]].begin(), pcs[opp_team[myteam]].end(),
				center.in(e_Dir(d))) != pcs[opp_team[myteam]].end())
				return e_Dir(d);
		}
	}
	return MAX_D;
}

e_Dir should_cs()
{
	if(neighb_teammate() != MAX_D)
		return MAX_D;
	return neighb_enemy();
}

e_Dir should_dig()
{
	// If there are enemies in view, don't dig.
	// Also, we don't want to just dig all the time; only dig with 1/5 chance:
	if(random()%5 || !pcs[opp_team[myteam]].empty())
		return MAX_D;
	// else:
	Coords c;
	e_Dir d = e_Dir(random()%MAX_D);
	for(char i = 0; i < MAX_D; ++i)
	{
	 	c = center.in(d);
		if(viewbuffer[(c.y*VIEWSIZE+c.x)*2+1] == '#')
			return d;
		++d;
	}
	return MAX_D;
}

inline bool mmsafe()
{
	return pcs[myteam].empty() && !pcs[opp_team[myteam]].empty();
}

// Returns true if a shootable target is found and puts the coordinates in 'target'
bool could_shoot(Coords &target)
{
	// Go through enemies in sight:
	vector<Coords>::const_iterator oti, eti;
	char r, line, ind;
	for(eti = pcs[opp_team[myteam]].begin(); eti != pcs[opp_team[myteam]].end(); ++eti)
	{
		if((r = center.dist_walk(*eti)) == 1) // enemy right next to us!
		{
			target = *eti;
			return true;
		}
		// Check that there are no teammates on the line of fire:
		if(eti->y == -r) line = eti->x + r;
		else if(eti->x == r) line = 3*r + eti->y;
		else if(eti->y == r) line = 5*r - eti->x;
		else /* eti->x == -r */ line = 7*r - eti->y;
		for(ind = 0; ind < 2*r; ind += 2)
		{
			if(find(pcs[myteam].begin(), pcs[myteam].end(),
				Coords(loslookup[r-2][line*2*r+ind], loslookup[r-2][line*2*r+ind+1]))
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

bool get_sound_to_follow(Coords &t)
{
	for(int i = 1; i < VIEWSIZE*VIEWSIZE*2; i += 2)
	{
		// get first sound effect, not including center of view
		if(viewbuffer[i] == '!' && i != VIEWSIZE*(VIEWSIZE+1)+1)
		{
			t = Coords(((i-1)/2)%VIEWSIZE, ((i-1)/2)/VIEWSIZE);
			return true;
		}
	}
	return false;
}


// MAIN:
// ///////////////////////////////

int main(int argc, char *argv[])
{
	string sip;
	if(argc > 1)
		sip = argv[1];
	else
		sip = "127.0.0.1:12360";
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
		return 1;
	}

	// create the socket
	struct addrinfo hints;
	int rv;
	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	// get server info:
	if((rv = getaddrinfo(ip.c_str(), port.c_str(), &hints, &servinfo)))
	{
#ifdef BOTMSG
		cerr << "Error in getaddrinfo: " << gai_strerror(rv) << endl;
#endif
		return 1;
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
		return 1;
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
		return 1;
	}
	time_t sendtime;
	time(&sendtime);
	unsigned char mid;
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
				return -1;
			case MID_HELLO_NEWID:
				cur_id = recv_buffer.read_sh();
				goto connected;
			default: break;
			}
		} // received something
		usleep(10000); // 10ms
	} while(time(NULL) - sendtime < 2);
#ifdef BOTMSG
	cerr << "Did not get a reply from the server." << endl;
#endif
	close(s_me);
	freeaddrinfo(servinfo);
	return 1;

connected:
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
		urf.close();
	}
	short msglen;

	// Send spawn request: (no sense having spectator bots!)
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_SPAWN_AXN);
	send_buffer.add(cur_id);
	send_buffer.add((unsigned char)(random()%NO_CLASS));
	do_send();

	Coords shoot_targ;
	char limiter = 0; // used to prevent bots from repeating the same action too often
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
					extract_pcs();
				}
			}
#ifdef BOTMSG
			else if(mid == MID_ADD_MSG)
			{
				recv_buffer.read_ch(); // cpair
				string msg_str;
				for(mid = recv_buffer.read_ch(); mid > 0; --mid)
				{
					recv_buffer.read_str(msg_str);
					cout << "MSG: " << msg_str << endl;
				}
			}
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
				// new hp:
				myhp = static_cast<char>(recv_buffer.read_ch());
			}
			else if(mid == MID_STATE_CHANGE)
			{
				if((myclass = e_Class(recv_buffer.read_ch())) == NO_CLASS)
					myhp = 0; // was forced into a spectator
				myteam = e_Team(recv_buffer.read_ch());
				limiter = 0;
			}
			//TODO: FANCY AI SHIT!!
			else if(mid == MID_GAME_UPD) { }
			else if(mid == MID_TIME_UPD) { }
		} // received a msg
		else usleep(10000);
		
		if(myhp > 0) // When alive, do sum'n
		{
			if(reftimer.update() - last_sent_axn > 250)
			{
				if(wait_turns)
					--wait_turns;
				else
				{
					if(limiter)
						--limiter;
					// First check if we could do something special:
					if(myclass == C_HEALER && !limiter &&
						((rv = neighb_enemy()) != MAX_D || (rv = neighb_teammate()) != MAX_D))
					{
						send_action(XN_HEAL, rv); // poison enemy or heal temmate
						limiter = 5; // don't heal again for this many turns
					}
					else if(myclass == C_HEALER && !limiter && myhp <= 2*classes[C_HEALER].hp/3)
					{
						send_action(XN_HEAL, MAX_D); // heal self
						limiter = 3;
					}
					else if(myclass == C_WIZARD && mmsafe())
					{
						send_action(XN_MM); // cast magic missile
						wait_turns = 2;
					}
					else if(myclass == C_FIGHTER && (rv = should_cs()) != MAX_D)
					{
						send_action(XN_CIRCLE_ATTACK, rv); // do circle attack
						wait_turns = 3;
					}
					else if(myclass == C_MINER && (rv = should_dig()) != MAX_D)
					{
						send_action(XN_MINE, rv);
						wait_turns = 7;
					}
					else if(myclass == C_ARCHER && !limiter && could_shoot(shoot_targ))
					{
						send_action(XN_SHOOT, shoot_targ.x - VIEWSIZE/2, shoot_targ.y - VIEWSIZE/2); // fire arrow
						limiter = 2; // just to cut some slack...
					}
					else if(myclass == C_TRAPPER && !limiter && pcs[opp_team[myteam]].empty())
					{
						send_action(XN_SET_TRAP);
						wait_turns = 8;
						limiter = 40; // wait a good while before setting another one
					}
					else // try to walk
					{
						// If no enemies in sight, either move randomly or follow sounds:
						if(pcs[opp_team[myteam]].empty())
						{
							if(get_sound_to_follow(shoot_targ)) // have sound to follow
								try_walk_towards(shoot_targ, true); // don't walk on PCs
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
	} // for eva

	// Disconnect
	freeaddrinfo(servinfo);
	close(s_me);
	return 0;
}


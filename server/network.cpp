// Please see LICENSE file.
#ifndef MAPTEST
#include "network.h"
#include "settings.h"
#include "game.h"
#include "log.h"
#include "cmds.h"
#include "occent.h"
#include "actives.h"
#include "../common/utfstr.h"
#include "../common/util.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <ctime>
#include <vector>
#include <cerrno>

// this is visible outside so that other parts of the server can construct packets directly
namespace Network { SerialBuffer send_buffer; }

extern bool intermission;

namespace
{
using namespace std;

const unsigned char MAX_AXN_QUEUE_SIZE = 5;

SerialBuffer recv_buffer;

int s_me; // the socket for the server


// returns true on error
bool do_send_to(sockaddr_storage *sas, socklen_t len,
	SerialBuffer &sb = Network::send_buffer)
{
	return sendto(s_me, sb.getr(), sb.amount(), 0, (sockaddr*)sas, len) == -1;
}


// Reads from a message the player ID and maps it to a player.
// Returns the end iterator if no such player.
list<Player>::iterator who_is_this()
{
	unsigned short pid = recv_buffer.read_sh();
	for(list<Player>::iterator it = cur_players.begin(); it != cur_players.end(); ++it)
	{
		if(it->stats_i->ID == pid)
			return it;
		/* NOTE: in principle, a malignant client could register as one player and
		 * afterwards ID as another... A remedy would be to check the address, but
		 * atm it seems unnecessary. */
	}
	return cur_players.end();
}

void cheat_attempt(const list<Player>::iterator pit)
{
	Network::to_chat(pit->nick + " is malfunctioning or cheating.");
	Game::remove_player(pit, " auto-kicked.");
}

} // end local namespace


bool Network::startup()
{
	struct addrinfo hints, *servinfo, *p;
	bzero(&hints, sizeof(hints));
	switch(Config::int_settings[Config::IS_IPV])
	{
	case 0: hints.ai_family = AF_UNSPEC; break;
	case 4: hints.ai_family = AF_INET; break;
	case 6: hints.ai_family = AF_INET6;
	}
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP
	int rv;
	if((rv = getaddrinfo(NULL,
		lex_cast(Config::int_settings[Config::IS_PORT]).c_str(),
		&hints, &servinfo)) != 0)
	{
		cerr << "Error in getaddrinfo: " <<  gai_strerror(rv) << endl;
		return false;
	}
	for(p = servinfo; p; p = p->ai_next)
	{
		if((s_me = socket(p->ai_family, p->ai_socktype,	p->ai_protocol)) == -1)
			continue;
		if(bind(s_me, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(s_me);
			continue;
		}
		break; // created socket and bound
	}
	if(!p)
	{
		cerr << "Failed to bind socket!" << endl;
		return false;
	}
	// set socket to nonblock mode
	fcntl(s_me, F_SETFL, O_NONBLOCK);
	
	freeaddrinfo(servinfo);
	
	time_t rawtime;
	time(&rawtime);
	to_log(string("Startup on ") + ctime(&rawtime));
	return true;
}


void Network::shutdown()
{
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_QUIT);
	broadcast();
	close(s_me);
}


bool Network::receive_n_handle()
{
	sockaddr_storage them;
	socklen_t addr_len = sizeof(them);
	unsigned char mid;
	list<Player>::iterator pit;
	char handled = 0;
	short msglen;
	unsigned short ush;
	string s;
	while(handled++ < 20 && // handle at most 20 messages; then return control
		(msglen = recvfrom(s_me, recv_buffer.getw(), BUFFER_SIZE, 0,
			(sockaddr *)&them, &addr_len)) != -1)
	{
		mid = recv_buffer.read_ch();
		switch(mid)
		{
		case MID_HELLO:
		{
			// Check that interaction versions match:
			if(recv_buffer.read_sh() != INTR_VERSION)
			{
				send_buffer.clear();
				send_buffer.add((unsigned char)MID_HELLO_VERSION);
				do_send_to(&them, addr_len);
				break;
			}
			string pl_passw = "";
			ush = recv_buffer.read_sh();
			if(ush != 0xFFFF) // indicates a password has been sent, too
				recv_buffer.read_str(pl_passw);
			recv_buffer.read_str(s); // read nick
			if(num_syms(s) > MAX_NICK_LEN)
			{
				/* Client passed version control but has too long nick??
				 * That means it's a tampered client! Ignore the bastard. */
				break;
			}
			ush = player_hello(ush, pl_passw, s, them);
			// Note: player_hello can call something that uses send_buffer!
			send_buffer.clear();
			if(ush == ID_STEAL)
			{
				send_buffer.add((unsigned char)MID_HELLO_STEAL);
				do_send_to(&them, addr_len);
				break;
			}
			/*else*/ if(ush == IP_BANNED)
			{
				send_buffer.add((unsigned char)MID_HELLO_BANNED);
				do_send_to(&them, addr_len);
				break;
			}
			/*else*/ if(ush == SERVER_FULL)
			{
				send_buffer.add((unsigned char)MID_HELLO_FULL);
				do_send_to(&them, addr_len);
				break;
			}
			/*else*/ if(ush == JOIN_OK_ID_OK)
				send_buffer.add((unsigned char)MID_HELLO);
			else
			{
				send_buffer.add((unsigned char)MID_HELLO_NEWID);
				send_buffer.add(ush);
				send_buffer.add(pl_passw);
			}
			do_send_to(&them, addr_len);
			// Send greeting:
			s = Config::greeting_str();
			construct_msg(s, DEF_MSG_COL);
			do_send_to(&them, addr_len);
			list<Player>::iterator pit = (--cur_players.end());
			if(intermission) // send the miniview to the new player
			{
				do_send_to(&them, addr_len, Game::get_miniview());
				Game::construct_team_msgs(pit);
			}
			else
				Game::send_flag_upds(pit);
			time(&(pit->last_switched_cl));

			Game::send_state_change(pit);
			Game::send_team_upds(pit);
			to_chat(pit->nick + " connected.");
			break;
		}
		case MID_BOTHELLO:
		{
			send_buffer.clear();
			// Check that interaction versions match:
			if(recv_buffer.read_sh() != INTR_VERSION)
			{
				send_buffer.add((unsigned char)MID_HELLO_VERSION);
				do_send_to(&them, addr_len);
				to_log("Incompatible bot connection.");
				break;
			}
			ush = recv_buffer.read_sh();
			if((ush = bot_connect(ush, them)) <= HIGHEST_VALID_ID)
			{
				send_buffer.add((unsigned char)MID_HELLO_NEWID);
				send_buffer.add(ush);
				send_buffer.add((unsigned short)Config::int_settings[Config::IS_TURNMS]);
				do_send_to(&them, addr_len);
			}
			break;
		}
		case MID_QUIT:
			if((pit = who_is_this()) != cur_players.end())
				Game::remove_player(pit, " disconnected.");
			break;
		case MID_SAY_ALOUD:
			if(!intermission && (pit = who_is_this()) != cur_players.end()
				&& pit->is_alive() && voices.find(pit->own_pc->getpos()) == voices.end())
			{
				recv_buffer.read_str(s);
				// A proper client wouldn't send too long messages:
				if(num_syms(s) > MSG_WIN_X-3)
					cheat_attempt(pit);
				else
				{
					add_voice(pit->own_pc->getpos(), s);
					event_set.insert(pit->own_pc->getpos());
				}
			}
			break;
		case MID_SAY_CHAT:
			if((pit = who_is_this()) != cur_players.end())
			{
				recv_buffer.read_str(s);
				// Check if it is a !command
				if(!s.empty() && s[0] == '!'
					&& process_cmd(pit, s))
					break;
				// (process_cmd returns true if the command should not be
				// broadcast)

				// Else, we forward the message to all clients (and add the
				// speakers nick to the end). Note that we are using the
				// "receive buffer" for sending, in order to avoid copying.
				if(!pit->muted)
				{
					*(recv_buffer.getw()+1) = *(recv_buffer.getw()+2) = 0;
					recv_buffer.set_amount(msglen);
					recv_buffer.add(pit->nick);
					recv_buffer.add((unsigned char)pit->team);
					broadcast(0xFFFF, recv_buffer);
				}
			}
			break;
		case MID_VIEW_UPD:
			if((pit = who_is_this()) != cur_players.end())
			{
				ush = recv_buffer.read_sh();
				// 't' is an ack from a client "I've received the view
				// update for turn t'. But the message might be delayed;
				// must check if the player has acted since:
				if(ush > pit->last_heard_of)
					pit->last_heard_of = ush;
			}
			break;
		case MID_TAKE_ACTION:
			if(!intermission && (pit = who_is_this()) != cur_players.end()
				&& pit->action_queue.size() < MAX_AXN_QUEUE_SIZE)
			{
				// First check sequentiality. If this action is "timestamped" earlier than
				// the latest action we have actually received from this player, ignore it!
				ush = recv_buffer.read_sh(); // turn
				if(ush >= pit->last_acted_on_turn)
				{
					// This is the latest we've heard from this client:
					if(ush > pit->last_heard_of)
						pit->last_heard_of = ush;

					unsigned char an = recv_buffer.read_ch(); // action number for the turn
					if(ush > pit->last_acted_on_turn || an > pit->last_axn_number)
					{
						// we accept this action, so update the values:
						pit->last_acted_on_turn = ush;
						pit->last_axn_number = an;

						// Now, construct the action:
						if((mid = recv_buffer.read_ch()) == XN_SHOOT) // contains two more values
						{
							// If either coordinate is too large by abs-val (out of screen), kick
							char xcoord = static_cast<char>(recv_buffer.read_ch());
							char ycoord = static_cast<char>(recv_buffer.read_ch());
							if(abs(xcoord) > VIEWSIZE/2 || abs(ycoord) > VIEWSIZE/2
								|| (!xcoord && !ycoord)) // both zero is bad, too
							{
#ifdef DEBUG
								to_log("Read illegal shot coordinates ("
									+ lex_cast(xcoord) + ","
									+ lex_cast(ycoord) + ")!");
#endif
								cheat_attempt(pit);
								break;
							}
							Game::player_action(pit, Axn(mid, xcoord, ycoord));
						}
						else if(mid == XN_MOVE || mid == XN_ZAP || mid == XN_HEAL
							|| mid == XN_CIRCLE_ATTACK || mid == XN_MINE) // one more value
						{
							char dir = static_cast<char>(recv_buffer.read_ch());
							if(dir > MAX_D || dir < 0 || (dir == MAX_D && mid != XN_HEAL))
							{
#ifdef DEBUG
								to_log("Read illegal direction "
									+ lex_cast(dir) + " in "
									+ lex_cast(mid) + "-type xn!");
#endif
								cheat_attempt(pit);
								break;
							}
							Game::player_action(pit, Axn(mid, dir));
						}
						else
							Game::player_action(pit, Axn(mid));
					}
					// else delivery order got messed up
				}
				// else delivery order got messed up
			}
			break;
		case MID_SPAWN_AXN:
			if((pit = who_is_this()) != cur_players.end())
				Game::class_switch(pit, e_Class(recv_buffer.read_ch()));
			break;
		case MID_TEAM_SWITCH:
			if((pit = who_is_this()) != cur_players.end())
				Game::team_switch(pit);
			break;
		case MID_TEAM_INFO:
			if((pit = who_is_this()) != cur_players.end())
				Game::send_team_info(pit);
			break;
		default: break;
		}
	}
	return (handled == 1);
}


void Network::send_to_player(const Player &pl, SerialBuffer &sb)
{
	do_send_to(&(pl.address), sizeof(pl.address), sb);
}

void Network::broadcast(const unsigned short omit_id, SerialBuffer &sb)
{
	for(list<Player>::iterator i = cur_players.begin(); i != cur_players.end(); ++i)
	{
		if(i->stats_i->ID != omit_id)
			do_send_to(&(i->address), sizeof(i->address), sb);
	}
}


void Network::construct_msg(string &s, const unsigned char cpair)
{
	/* Note: all message are server-generated, and pretty much the only content
	 * unknown at compile time and beyond the admin's control are the player nicks,
	 * which have limited lenght. Hence, we can rest assured that the strings do
	 * not contain words of lenght > MSG_WIN_X (=57). */
	vector<string> blocks;
	size_t i;
	// break up at spaces:
	while(num_syms(s) > MSG_WIN_X)
	{
		if((i = s.rfind(' ', MSG_WIN_X)) == string::npos)
			i = MSG_WIN_X;
		blocks.push_back(s.substr(0, i));
		s.erase(0, i+1);
	}
	if(!s.empty())
		blocks.push_back(s);
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_ADD_MSG);
	send_buffer.add(cpair);
	send_buffer.add((unsigned char)blocks.size());
	for(i = 0; i < blocks.size(); ++i)
		send_buffer.add(blocks[i]);
}


void Network::to_chat(const string &s)
{
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_SAY_CHAT);
	send_buffer.add((unsigned short)0);
	send_buffer.add(s);
	send_buffer.add(string("* "));
	send_buffer.add((unsigned char)T_NO_TEAM); // no team, server speaking
	broadcast();
}


void Network::clocksync(const unsigned short time, const unsigned char spawn)
{
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_TIME_UPD);
	send_buffer.add(time);
	send_buffer.add(spawn);
	broadcast();
}

#endif // not maptest build

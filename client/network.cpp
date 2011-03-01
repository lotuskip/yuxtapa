// Please see LICENSE file.
#include "network.h"
#include "settings.h"
#include "../common/netutils.h"
#include "../common/constants.h"
#include "../common/timer.h"
#include "view.h"
#include "base.h"
#include "msg.h"
#include "class_cpv.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>
#include <ctime>
#include <boost/lexical_cast.hpp>
#include <ncurses.h>

namespace
{
using namespace std;

const char CONNECTION_ATTEMPTS = 3;
const short REPLY_WAIT_MS = 2000; // wait this long for server reply
SerialBuffer recv_buffer, send_buffer;
const string serversfile_name = "servers";

const string hello_errors[] = {
	"The server is full.", 
	"You are banned from this server.",
	"Server is running incompatible version.",
	"Someone is using your ID on this server..." };

int s_me; // socket
struct addrinfo *servinfo, *the_serv;
unsigned short cur_id;

// This is the number of the turn we last go a view update for:
unsigned short curturn = 0;
// This counts the actions we have taken on the current turn. This acts as a
// timestamp; since the packet delivery isn't guaranteed to be sequentially
// ordered, we must assure the server gets the actions in the intended order!
unsigned char axn_counter = 0;

msTimer last_sent_axn, reftimer;

bool do_send() // returns true if there was a problem
{
	return sendto(s_me, send_buffer.getr(), send_buffer.amount(), 0,
		the_serv->ai_addr, the_serv->ai_addrlen) == -1;
}

sockaddr them;
socklen_t addr_len;
short do_receive() // returns lenght of data received
{
	addr_len = sizeof(them);
	return recvfrom(s_me, recv_buffer.getw(), BUFFER_SIZE, 0, &them, &addr_len);
}


void add_id_info_for_server(const string &serverip)
{
	cur_id = 0xFFFF;
	ifstream file((Config::configdir() + serversfile_name).c_str());
	string ipstr, passw;
	unsigned short sh;
	while(file)
	{
		// NOTE: not very error tolerant; users should not mess with the file
		file >> ipstr;
		file >> sh;
		file >> passw;
		if(ipstr == serverip) // match
		{
			cur_id = sh;
			break;
		}
	}
	send_buffer.add(cur_id);
	if(cur_id != 0xFFFF) // some ID was found
		send_buffer.add(passw);
}

void store_id_for_server(const unsigned short id,
	const string &serverip, const string &passw)
{
	string line2add = serverip + ' '
		+ boost::lexical_cast<string>(id) + ' ' + passw;
	// The above was a clean "lookup", but here we might need to replace...
	vector<string> lines;
	ifstream ifile((Config::configdir() + serversfile_name).c_str());
	string tmp;
	bool not_replaced = true;
	while(getline(ifile, tmp))
	{
		if(not_replaced && tmp.find(serverip) == 0) // this is the server
		{
			tmp = line2add;
			not_replaced = false;
		}
		lines.push_back(tmp);
	}
	ifile.close();
	if(not_replaced)
		lines.push_back(line2add);

	ofstream ofile((Config::configdir() + serversfile_name).c_str());
	while(!lines.empty())
	{
		ofile << lines.front() << endl;
		lines.erase(lines.begin());
	}
}

} // end local namespace

extern e_ClientState clientstate;


bool Network::connect(string &errors)
{
	// determine ip and port
	string sip = Config::get_server_ip(); // this is just what was read from the config; could be garbage!
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
		errors = "Not a valid address: " + sip;
		return false;
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
		errors = string("Error in getaddrinfo: ") + gai_strerror(rv);
		return false;
	}
	// initialize the_serv and make the socket based on its protocol info:
	for(the_serv = servinfo; the_serv; the_serv = the_serv->ai_next)
	{
		if((s_me = socket(the_serv->ai_family, the_serv->ai_socktype, the_serv->ai_protocol)) != -1)
			break;
	}
	if(!the_serv)
	{
		errors = "Failed to create socket!";
		freeaddrinfo(servinfo);
		return false;
	}
	// set socket to nonblock mode:
	fcntl(s_me, F_SETFL, O_NONBLOCK);

	last_sent_axn.update(); // must init this at some point

	add_msg("Connecting to " + sip + "...", 7);
	// construct a hello message:
	send_buffer.add((unsigned char)MID_HELLO);
	send_buffer.add(INTR_VERSION);
	add_id_info_for_server(sip);
	send_buffer.add(Config::get_nick());
	
	// send hello message (repeatedly) and wait for a reply
	int k;
	for(char j = 0; j < CONNECTION_ATTEMPTS; ++j)
	{
		if(do_send())
		{
			errors = "Problem sending connection request.";
			close(s_me);
			freeaddrinfo(servinfo);
			return false;
		}
		// Wait for the reply (the timeout on the getch() call will delay):
		for(k = 0; k < REPLY_WAIT_MS/GETCH_TIMEOUT; ++k)
		{
			if(do_receive() != -1)
			{
				// got *some* message
				unsigned char mid = recv_buffer.read_ch();
				switch(mid)
				{
				case MID_HELLO:
					Base::print_str("(identified)", 7, 0, 5, STAT_WIN, false);
					return true;
				case MID_HELLO_FULL:
				case MID_HELLO_BANNED:
				case MID_HELLO_VERSION:
				case MID_HELLO_STEAL:
					errors = hello_errors[mid - MID_HELLO_FULL];
					close(s_me);
					freeaddrinfo(servinfo);
					return false;
				case MID_HELLO_NEWID:
				{
					cur_id = recv_buffer.read_sh();
					string passw;
					recv_buffer.read_str(passw);
					store_id_for_server(cur_id, sip, passw);
					Base::print_str("(new id)", 7, 0, 5, STAT_WIN, false);
					return true;
				}
				default: break; // ignore other messages
				}
			} // received something
			if(getch() == KEYCODE_INT)
			{
				errors = "Aborted by user.";
				return false;
			}
		} // listen for connections loop
	} // send connection requests loop
	errors = "Did not get a reply from the server.";
	close(s_me);
	freeaddrinfo(servinfo);
	return false;
}


void Network::disconnect()
{
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_QUIT);
	send_buffer.add(cur_id);
	do_send();
	freeaddrinfo(servinfo);
	close(s_me);
}


bool Network::receive_n_handle()
{
	unsigned char mid;
	char handled = 0;
	short msglen;
	while(handled++ < 5 && // handle at most 5 messages; then return control
		(msglen = do_receive()) != -1)
	{
		mid = recv_buffer.read_ch();
		switch(mid)
		{
		case MID_QUIT:
			return true;
		case MID_VIEW_UPD:
		{
			curturn = recv_buffer.read_sh(); // new turn number
			// NOTE: if an earlier turn update should arrive later than a
			// newer one, we might "fall back". This is rare enough to
			// bypass, hopefully.
			axn_counter = 0;

			// Return a "pong":
			send_buffer.clear();
			send_buffer.add((unsigned char)MID_VIEW_UPD);
			send_buffer.add(cur_id);
			send_buffer.add(curturn);
			do_send();

			if(msglen > 5) // msg is not very small => view has changed
			{
				extern char viewbuffer[BUFFER_SIZE]; // defined in view.cpp
				recv_buffer.read_compressed(viewbuffer);
				if(clientstate != CS_LIMBO && clientstate != CS_HELP)
					redraw_view();
			}
			Base::viewtick();
			break;
		}
		case MID_ADD_MSG:
		{
			unsigned char cp = recv_buffer.read_ch();
			string msg_str;
			for(mid = recv_buffer.read_ch(); mid > 0; --mid)
			{
				recv_buffer.read_str(msg_str);
				add_msg(msg_str, cp);
			}
			break;
		}
		case MID_SAY_CHAT:
		{
			recv_buffer.read_sh(); // skip id
			string line;
			recv_buffer.read_str(line);
			string speaker_nick;
			recv_buffer.read_str(speaker_nick);
			add_to_chat(line, speaker_nick, recv_buffer.read_ch());
			break;
		}
		case MID_STATE_UPD:
			ClassCPV::state_upd(recv_buffer);
			break;
		case MID_STATE_CHANGE:
			mid = recv_buffer.read_ch(); // class
			ClassCPV::state_change(mid, recv_buffer.read_ch()); // and team
			break;
		case MID_GAME_UPD:
		{
			mid = recv_buffer.read_ch(); // # of green
			unsigned char ch = recv_buffer.read_ch(); // # of purples
			string st_str1, st_str2;
			recv_buffer.read_str(st_str1);
			recv_buffer.read_str(st_str2);
			if(!st_str2.empty())
				st_str1 += ", " + st_str2;
			Base::print_teams_upd(mid, ch, st_str1);	
			break;
		}
		case MID_FLAG_UPD:
		{
			string tmp;
			for(unsigned char ch = recv_buffer.read_ch(); ch > 0; --ch)
				tmp += recv_buffer.read_ch();
			Base::print_flags(tmp, ClassCPV::get_cur_team_col());
			break;
		}
		case MID_TIME_UPD:
		{
			unsigned short sh = recv_buffer.read_sh();
			clocksync(sh, recv_buffer.read_ch());
			break;
		}
		default: break;
		}
	}
	return false;
}


void Network::send_action(const unsigned char xncode, const unsigned char var1, const unsigned char var2)
{
	// Nice client behaviour: don't send way too often:
	if(reftimer.update() - last_sent_axn > 50) // every 50 ms at most?
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
		last_sent_axn.update();
	}
}


void Network::send_line(const string &s, const bool chat)
{
	send_buffer.clear();
	if(chat)
		send_buffer.add((unsigned char)MID_SAY_CHAT);
	else
		send_buffer.add((unsigned char)MID_SAY_ALOUD);
	send_buffer.add(cur_id);
	send_buffer.add(s);
	do_send();
}

void Network::send_spawn(const unsigned char newclass)
{
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_SPAWN_AXN);
	send_buffer.add(cur_id);
	send_buffer.add(newclass);
	do_send();	
}

void Network::send_switch()
{
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_TEAM_SWITCH);
	send_buffer.add(cur_id);
	do_send();	
}


bool Network::not_acted() { return !axn_counter; }


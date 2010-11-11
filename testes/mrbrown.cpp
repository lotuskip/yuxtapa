// Please see LICENSE file.
#include "../common/netutils.h"
#include "../common/timer.h"
#include "../common/classes_common.h"
#include "../common/coords.h"
#include "../common/col_codes.h"
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
char viewbuffer[BUFFER_SIZE];

const Coords center(VIEWSIZE/2, VIEWSIZE/2);
bool no_walk_to(const e_Dir d)
{
	Coords c = center.in(d);
	// colour is at (y*SIZE+x)*2, symbol at that+1
	char sym = viewbuffer[(c.y*VIEWSIZE+c.x)*2+1];
	return (sym == '?' || sym == '~' || sym == '#');
}

msTimer last_sent_axn, reftimer;

bool do_send()
{
	return sendto(s_me, send_buffer.getr(), send_buffer.amount(), 0,
		the_serv->ai_addr, the_serv->ai_addrlen) == -1;
}

sockaddr them;
size_t addr_len;
short do_receive()
{
	addr_len = sizeof(them);
	return recvfrom(s_me, recv_buffer.getw(), BUFFER_SIZE, 0, &them, &addr_len);
}

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
	 * have the same random seed. Instead, seed from dev/randon: */
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
	bool alive = false;

	// Send spawn request: (no sense having spectator bots!)
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_SPAWN_AXN);
	send_buffer.add(cur_id);
	send_buffer.add((unsigned char)(random()%NO_CLASS));
	do_send();

	e_Dir walkdir;
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
					recv_buffer.read_compressed(viewbuffer);
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
				// new hp tell us whether we are alive:
				alive = (static_cast<char>(recv_buffer.read_ch()) > 0);
			}
			//TODO: FANCY AI SHIT!!
			else if(mid == MID_STATE_CHANGE) { }
			else if(mid == MID_GAME_UPD) { }
			else if(mid == MID_TIME_UPD) { }
		} // received a msg
		else usleep(10000);
		
		if(alive) // When alive, get movin'
		{
			//TODO: fancy AI shit here, as well.

			if(reftimer.update() - last_sent_axn > 250)
			{
				// Figure out a dir to walk to (avoid chasms&water and don't run into walls)
				walkdir = e_Dir(random()%MAX_D);
				for(rv = 0; rv < MAX_D; ++rv)
				{
					if(!no_walk_to(walkdir)) // can walk there
						break;
					++walkdir;
				}
				if(rv < MAX_D) // found a dir to walk to
				{
					send_buffer.clear();
					send_buffer.add((unsigned char)MID_TAKE_ACTION);
					send_buffer.add(cur_id);
					send_buffer.add(curturn);
					send_buffer.add(axn_counter);
					send_buffer.add((unsigned char)XN_MOVE);
					send_buffer.add((unsigned char)walkdir);
					do_send();
					++axn_counter;
				} // else we're stuck or something...
				last_sent_axn.update();
			}
		}
	} // for eva

	// Disconnect
	freeaddrinfo(servinfo);
	close(s_me);
	return 0;
}


// Please see LICENSE file.
#include "../common/netutils.h"
#include "../common/timer.h"
#include "../common/classes_common.h"
#include "../common/coords.h"
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
#ifndef BE_QUIET
#include <iostream>
#endif

using namespace std;

const string mynick = "Mr. Brown";

#ifndef BE_QUIET
const string hello_errors[] = {
	"The server is full.", 
	"Bot IP banned??",
	"Server is running incompatible version." };
#endif

SerialBuffer recv_buffer, send_buffer;
int s_me; // socket
struct addrinfo *servinfo, *the_serv;
unsigned short cur_id;
unsigned short curturn = 0;
unsigned char axn_counter = 0;

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
	if((i = sip.find(':')) != string::npos)
	{
		port = sip.substr(i+1);
		ip = sip.substr(0, i);
	}
	else
	{
#ifndef BE_QUIET
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
#ifndef BE_QUIET
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
#ifndef BE_QUIET
		cerr << "Failed to create socket!" << endl;
#endif
		freeaddrinfo(servinfo);
		return 1;
	}
	// set socket to nonblock mode:
	fcntl(s_me, F_SETFL, O_NONBLOCK);

	// construct a hello message:
	send_buffer.add((unsigned char)MID_HELLO);
	send_buffer.add(GAME_VERSION);
	send_buffer.add((unsigned short)0xFFFF);
	send_buffer.add(mynick);
	
	last_sent_axn.update();

	if(do_send())
	{
#ifndef BE_QUIET
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
			case MID_HELLO_FULL:
			case MID_HELLO_BANNED:
			case MID_HELLO_VERSION:
#ifndef BE_QUIET
				cerr << hello_errors[mid - MID_HELLO_FULL] << endl;
#endif
				close(s_me);
				freeaddrinfo(servinfo);
			case MID_HELLO_NEWID:
				cur_id = recv_buffer.read_sh();
				goto connected;
			default: break;
			}
		} // received something
	} while(time(NULL) - sendtime < 2);
#ifndef BE_QUIET
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
#ifndef BE_QUIET
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

	// Tell server not to stat us:
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_SAY_CHAT);
	send_buffer.add(cur_id);
	send_buffer.add(string("!dontstatme"));
	do_send();

	// Send spawn request: (no sense having spectator bots!)
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_SPAWN_AXN);
	send_buffer.add(cur_id);
	send_buffer.add((unsigned char)(random()%NO_CLASS));
	do_send();

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

				// Could grab the view and do some fancy AI shit with it...
			}
#ifndef BE_QUIET
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
#if 0
			//TODO: FANCY AI SHIT!!
			else if(mid == MID_STATE_CHANGE) { }
			else if(mid == MID_GAME_UPD) { }
			else if(mid == MID_TIME_UPD) { }
#endif
		} // received a msg
		else usleep(10000);
		
		if(alive) // When alive, get movin'
		{
			//TODO: Could do fancy AI shit here, as well.

			if(reftimer.update() - last_sent_axn > 250)
			{
				send_buffer.clear();
				send_buffer.add((unsigned char)MID_TAKE_ACTION);
				send_buffer.add(cur_id);
				send_buffer.add(curturn);
				send_buffer.add(axn_counter);
				send_buffer.add((unsigned char)XN_MOVE);
				send_buffer.add((unsigned char)(random()%MAX_D));
				do_send();
				++axn_counter;
				last_sent_axn.update();
			}
		}
	} // for eva

	// Disconnect
	send_buffer.clear();
	send_buffer.add((unsigned char)MID_QUIT);
	send_buffer.add(cur_id);
	do_send();
	freeaddrinfo(servinfo);
	close(s_me);
	return 0;
}


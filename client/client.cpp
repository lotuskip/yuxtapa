// Please see LICENSE file.
#include "base.h"
#include "settings.h"
#include "network.h"
#include "input.h"
#include "msg.h"
#include "../common/constants.h"
#include <clocale>
#include <iostream>

namespace
{
using namespace std;

const unsigned char MAX_SERVERINFO_X = 24;

void fix_str_to_fit(string &s)
{
	if(s.size() > MAX_SERVERINFO_X)
	{
		s.erase(MAX_SERVERINFO_X-3);
		s += "...";
	}
}

}


int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "");

	string server = "";
	if(argc >= 2) // command line argument should be a server name
	{
		if((server = argv[1]) == "--help" || server == "-h")
		{
			cout << "Flag \'-v\' prints the version. Other than that, any command "
				"line argument should be either a server address or a server name "
				"defined in the configuration." << endl;
			return 0;
		}
		if(server == "-v" || server == "--version")
		{
			cout << PACKAGE << " client v." << VERSION << endl;
			return 0;
		}
	}
	Config::read_config(server);
	if(!Base::init_ncurses())
		return 1;
	init_msgs();
	// reuse server-string for connection errors:
	if(!Network::connect(server))
	{
		Base::deinit_ncurses();
		cerr << server << endl;
		return 1;
	}
	// If here, we are connected, and 'server' string is unmodified.
	// Print server info, which is constant for the whole session:
	server.insert(0, "Server: ");
	fix_str_to_fit(server);
	Base::print_str(server.c_str(), 7, 0, 3, STAT_WIN, false);
	// an IPv6 address might be too long to fit on one line:
	fix_str_to_fit((server = Config::get_server_ip()));
	Base::print_str(server.c_str(), 7, 0, 4, STAT_WIN, false);
	
	//Config::get_server_ip() jaettuna kahdella riville tarvittaessa

	bool server_disconnect = false;
	for(;;)
	{
		if(Network::receive_n_handle())
		{
			server_disconnect = true;
			break;
		}
		if(Input::inputhandle())
			break;
		upd_msgs();
	}

	Network::disconnect();
	Base::deinit_ncurses();
	if(server_disconnect)
		cout << "Server disconnected." << endl;
	return 0;
}

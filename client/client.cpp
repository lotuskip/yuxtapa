// Please see LICENSE file.
#include "base.h"
#include "settings.h"
#include "network.h"
#include "input.h"
#include "msg.h"
#include "../common/constants.h"
#include <clocale>
#include <iostream>

using namespace std;

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

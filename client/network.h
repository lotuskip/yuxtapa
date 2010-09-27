// Please see LICENSE file.
#ifndef CLIENT_NETWORK_H
#define CLIENT_NETWORK_H

#include <string>

namespace Network
{
	bool connect(); // connect to IP set in config
	void disconnect();

	bool receive_n_handle(); // returns true if should exit

	void send_action(const unsigned char xncode, const unsigned char var1 = 0,
		const unsigned char var2 = 0);
	void send_line(const std::string &s, const bool chat); // !chat => shout
	void send_spawn(const unsigned char newclass);
	void send_switch(); // to switch team

	bool not_acted();
}

#endif

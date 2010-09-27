// Please see LICENSE file.
#ifndef SERVER_NETWORK_H
#define SERVER_NETWORK_H

#include "players.h"
#include "../common/netutils.h"

namespace Network
{
	bool startup(); // returns true on success
	void shutdown();

	bool receive_n_handle(); // returns true if there are no packets

	// This is the default buffer to use for messages that are send once and then
	// forgotten about. If parts of the server need to keep the message ready for
	// further resending, they use their own buffers.
	extern SerialBuffer send_buffer;
	
	void send_to_player(const Player &pl, SerialBuffer &sb = send_buffer);
	void broadcast(const unsigned short omit_id = 0xFFFF, // default: omit no one
		SerialBuffer &sb = send_buffer);

	// This will construct a (list of) message(s) into send_buffer,
	// and the call should be followed by a send_to_player/broadcast.
	// Note that 's' will be modified.
	void construct_msg(std::string &s, const unsigned char cpair);

	// This sends a line to the chat (and broadcasts):
	void to_chat(const std::string &s);
	
	void clocksync(const unsigned short time, const unsigned char spawn);
}

#endif

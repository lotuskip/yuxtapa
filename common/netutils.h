//Please see LICENSE file.
#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <string>
#include <boost/array.hpp>
#include "constants.h"

// Message identifiers
enum {
// both ways:
	MID_HELLO=0, // server sends this to indicate the connection is accepted
	MID_BOTHELLO, // only bots->S
	MID_QUIT,
	MID_VIEW_UPD, /* S->C: new view or a "next turn" notice, also acts as a
		'ping'. C->S: the corresponding 'pong' */
	MID_SAY_CHAT,
// C to S only:
	MID_SAY_ALOUD,
	MID_TAKE_ACTION,
	MID_SPAWN_AXN, // join team and/or change class
	MID_TEAM_SWITCH, // switch team
// S to C only:
	MID_ADD_MSG, // msg to status window, possibly several
	MID_STATE_UPD, // variable PC info has changed
	MID_STATE_CHANGE, // class or team has changed (redraw entire PC info)
	MID_GAME_UPD, // the amounts of players in the teams and objective update
	MID_TIME_UPD, // clock sync
	// various replies to client hello:
	MID_HELLO_FULL, // kick, we're full
	MID_HELLO_BANNED, // kick, you're banned
	MID_HELLO_VERSION, // kick, incompatible version
	MID_HELLO_STEAL, // kick, someone is using your nick
	MID_HELLO_NEWID, // player accepted but given a new id
// above that everything is unknown:
	MAX_MID
};

// The actions coded into MID_TAKE_ACTION:
enum {
	// class independent:
	XN_CLOSE_DOOR=0,
	XN_TORCH_HANDLE,
	XN_TRAP_TRIGGER,
	XN_SUICIDE,
	XN_MOVE, // + dir
	// class dependents; note that order is the same as that of the classes
	XN_SHOOT, // + coords
	XN_FLASH,
	XN_ZAP, // + dir
	XN_BLINK,
	XN_DISGUISE,
	XN_CIRCLE_ATTACK, // + dir
	XN_MINE, // + dir
	XN_HEAL, // + dir, can be MAX_D for self
	XN_MM,
	XN_SET_TRAP,
	// finally, for deads&specs:
	XN_FOLLOW_SWITCH,
	XN_FOLLOW_PREV
};


// Serialization buffer for sending and receiving:
class SerialBuffer
{
public:
	SerialBuffer();
	~SerialBuffer() {}
	
	void clear();

	/*
	 * Writing (sending)
	 */

	// the only datatypes we ever send raw:
	void add(const unsigned char ch);
	void add(const unsigned short sh);
	void add(const std::string &str);

	void write_compressed(char *buffer, const unsigned short len);

	const char *getr() const { return arr.data(); }
	int amount() const { return num; }

	/*
	 * Reading (receiving)
	 */

	unsigned short read_sh();
	unsigned char read_ch();
	void read_str(std::string &target);

	void read_compressed(char *buffer);
	char *getw(); // this time for writing; this calls clear()

	// this is used when we forward (a part of) the same message
	void set_amount(const short n);

private:
	boost::array<char, BUFFER_SIZE>::iterator pos;
	short num; // this would suffice, but we have pos for convenience, too
	boost::array<char, BUFFER_SIZE> arr;
};

#endif

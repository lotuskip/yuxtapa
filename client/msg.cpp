// Please see LICENSE file.
#include "msg.h"
#include "base.h"
#include "../common/utfstr.h"
#include "../common/constants.h"
#include "../common/timer.h"
#include "../common/col_codes.h"
#include "boost/lexical_cast.hpp"

namespace
{
using namespace std;

/*
 * For message window:
 */

// messages on the screen:
deque<string> msgbuffer(MSG_WIN_Y-1); // -1 to leave last row for typing space
deque<unsigned char> msgcpairs(MSG_WIN_Y-1);
char bottom_idx = 0;
// messages awaiting to go onto the screen:
deque<string> msgavail;
deque<unsigned char> availcpairs;

// Messages are added (to the screen) at most every STAY_TIME ms.
// Messages are moved away at least every MOVE_FREQ ms.
const unsigned short MOVE_FREQ = 3600; // ms
const unsigned short STAY_TIME = 800;
msTimer last_move, last_add, reference;

/*
 * For chat window:
 */
const char CHAT_BUFFER_SIZE = 50;

// cf. e_Team im ../server/players.h
const char* team_ind[4] = { "* ", ": ", " @: ", " @: " };
const unsigned char team_cpair[4] = { 0, C_NEUT_FLAG, C_GREEN_PC, C_PURPLE_PC };

deque<string> chatbuffer;
deque<string> chatspeaker;
deque<unsigned char> chatteams;
char chat_top = 0;
char num_chat_lines = 0;

/*
 * For clocks:
 */
unsigned short maptime = 0;
unsigned char spawntime = 0;
msTimer last_second;


void redraw_chat()
{
	char num = min(int(Base::num_chat_lines_to_show()), int(num_chat_lines - chat_top));
	for(char i = 0; i < num; ++i)
	{
		Base::incr_print_start(0, i, CHAT_WIN);
		Base::incr_print(chatspeaker[chat_top+i].c_str(), 3, CHAT_WIN);
		Base::incr_print(team_ind[chatteams[chat_top+i]],
			team_cpair[chatteams[chat_top+i]], CHAT_WIN);
		Base::incr_print(chatbuffer[chat_top+i].c_str(), 7, CHAT_WIN);
		Base::incr_print_end(CHAT_WIN, true);
	}
	// if necessary, print indicators of there being more messages:
	if(chat_top)
		Base::less_chat_ind();
	if(chat_top + num < num_chat_lines)
		Base::more_chat_ind();
}

void move_msgs()
{
	if(bottom_idx)
	{
		msgbuffer.pop_front();
		msgbuffer.push_back("");
		msgcpairs.pop_front();
		msgcpairs.push_back(0);
		--bottom_idx;
		last_move.update();
	}
}

void redraw_clocks()
{
	unsigned short mins = maptime/60;
	unsigned short secs = maptime%60;
	string timestr = "";
	if(mins < 10)
		timestr += ' ';
	timestr += boost::lexical_cast<string>(mins);
	timestr += ':';
	if(secs < 10)
		timestr += '0';
	timestr += boost::lexical_cast<string>(secs);
	Base::print_str(timestr.c_str(), 7, 2, 10, PC_WIN, true);

	timestr.clear();
	if(spawntime < 10)
		timestr += ' ';
	timestr += boost::lexical_cast<string>((unsigned short)spawntime);
	Base::print_str(timestr.c_str(), 7, 5, 11, PC_WIN, true);
}

} // end local namespace


void init_msgs()
{
	last_move.update();
	last_add.update();

	last_second.update();
}


void add_msg(const string &s, const unsigned char cp)
{
	if(bottom_idx == MSG_WIN_Y-1) // screen buffer full
	{
		// check if STAY_TIME has passed:
		if(reference.update() - last_add < STAY_TIME)
		{
			// time not passed, put the message in queue to be displayed later
			msgavail.push_back(s);
			availcpairs.push_back(cp);
			return;
		}
		else
			move_msgs();
	}
	msgbuffer[bottom_idx] = s;
	msgcpairs[bottom_idx] = cp;
	++bottom_idx;
	Base::redraw_msgs(msgbuffer, msgcpairs);
	last_add.update();
	last_move.update(); // also delay moving
}

void upd_msgs()
{
	// add messages in avail, if any:
	if(!msgavail.empty() && reference.update() - last_add >= STAY_TIME)
	{
		move_msgs();
		msgbuffer[bottom_idx] = msgavail.front();
		msgcpairs[bottom_idx] = availcpairs.front();
		msgavail.pop_front();
		availcpairs.pop_front();
		++bottom_idx;
		last_add.update();
		Base::redraw_msgs(msgbuffer, msgcpairs);
	}
	else if(reference.update() - last_move >= MOVE_FREQ)
	{
		move_msgs();
		Base::redraw_msgs(msgbuffer, msgcpairs);
	}

	// clock check:
	if(reference.update() - last_second >= 1000)
	{
		if(maptime)
			--maptime;
		if(spawntime)
			--spawntime;
		redraw_clocks();
		last_second.update();
	}
}

void clear_msgs()
{
	for(char i = 0; i < bottom_idx; ++i)
		msgbuffer[i].clear(); // never mind the cpairs
	bottom_idx = 0;
	Base::redraw_msgs(msgbuffer, msgcpairs);
}


void add_to_chat(const string &s, const string &talker, const unsigned char t)
{
	bool forceredraw = false;
	if(num_chat_lines == CHAT_BUFFER_SIZE)
	{
		chatbuffer.pop_front();
		chatspeaker.pop_front();
		chatteams.pop_front();
		if(!chat_top)
			forceredraw = true;
		else
			--chat_top;
	}
	else
		++num_chat_lines;

	chatbuffer.push_back(s);
	chatspeaker.push_back(talker);
	chatteams.push_back(t);

	// auto-scrolling:
	if(chat_top + Base::num_chat_lines_to_show() + 1 == num_chat_lines)
	{
		++chat_top;
		forceredraw = true;
	}

	// Redraw if the new line is shown or the deleted line was shown before
	if(forceredraw ||
		chat_top + Base::num_chat_lines_to_show() + 1 >= num_chat_lines)
		redraw_chat();
}

void scroll_chat_up()
{
	if(chat_top + Base::num_chat_lines_to_show() < num_chat_lines)
	{
		++chat_top;
		redraw_chat();
	}
}

void scroll_chat_down()
{
	if(chat_top)
	{
		--chat_top;
		redraw_chat();
	}
}



void clocksync(const unsigned short t, const unsigned char spawn)
{
	maptime = t;
	spawntime = spawn;
	redraw_clocks();
	last_second.update();
}


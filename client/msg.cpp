// Please see LICENSE file.
#include "msg.h"
#include "base.h"
#include "../common/utfstr.h"
#include "../common/constants.h"
#include "../common/timer.h"
#include "../common/col_codes.h"
#include <boost/lexical_cast.hpp>
#include <deque>
#include <cstring>

namespace
{
using namespace std;

/*
 * For message window:
 */

// messages on the screen:
struct MsgBufferLine
{
	string text;
	unsigned char cpair;
	MsgBufferLine(const string &s = "", const unsigned char c = 0)
		: text(s), cpair(c) {}
};
deque<MsgBufferLine> msgbuffer(MSG_WIN_Y-1); // -1 to leave last row for typing space
char bottom_idx = 0;
// messages awaiting to go onto the screen:
deque<MsgBufferLine> msgavail;

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
const char* team_ind[4] = { " @: ", " @: ", ": ", "" };

struct ChatBufferLine
{
	string text, speaker;
	unsigned char team;
	ChatBufferLine(const string &m = "", const string &s = "",
		const unsigned char t = 0) : text(m), speaker(s), team(t) {}
};
deque<ChatBufferLine> chatbuffer;
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
		Base::incr_print(chatbuffer[chat_top+i].speaker.c_str(), 3, CHAT_WIN);
		Base::incr_print(team_ind[chatbuffer[chat_top+i].team],
			team_colour[chatbuffer[chat_top+i].team], CHAT_WIN);
		Base::incr_print(chatbuffer[chat_top+i].text.c_str(), 7, CHAT_WIN);
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
		msgbuffer.push_back(MsgBufferLine());
		--bottom_idx;
		last_move.update();
	}
}

void redraw_msgs()
{
	for(char y = 0; y < MSG_WIN_Y-1; ++y)
		Base::print_str(msgbuffer[y].text.c_str(), msgbuffer[y].cpair, 0, y,
			MSG_WIN, true);
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
	Base::print_str(timestr.c_str(), 7, 51, 6, STAT_WIN, true);

	timestr.clear();
	if(spawntime < 10)
		timestr += ' ';
	timestr += boost::lexical_cast<string>((unsigned short)spawntime);
	Base::print_str(timestr.c_str(), 7, 54, 7, STAT_WIN, true);
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
			msgavail.push_back(MsgBufferLine(s,cp));
			return;
		}
		else
			move_msgs();
	}
	msgbuffer[bottom_idx].text = s;
	msgbuffer[bottom_idx].cpair = cp;
	++bottom_idx;
	redraw_msgs();
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
		msgavail.pop_front();
		++bottom_idx;
		last_add.update();
		redraw_msgs();
	}
	else if(reference.update() - last_move >= MOVE_FREQ)
	{
		move_msgs();
		redraw_msgs();
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
		msgbuffer[i].text.clear(); // nevermind the cpairs
	bottom_idx = 0;
	redraw_msgs();
}


void add_to_chat(string &s, const string &talker, const unsigned char t)
{
	// Chop into pieces if line is too long:
	short len = talker.size() + strlen(team_ind[t]);
	if(len + s.size() > Base::chat_width())
	{
		unsigned int i;
		if((i = s.rfind(' ', Base::chat_width() - len)) == string::npos)
			i = Base::chat_width() - len;
		chatbuffer.push_back(ChatBufferLine(s.substr(0, i), talker, t));
		++num_chat_lines;
		s.erase(0, i);
		while(s.size() > Base::chat_width())
		{
			if((i = s.rfind(' ', Base::chat_width())) == string::npos)
				i = Base::chat_width();
			chatbuffer.push_back(ChatBufferLine(s.substr(0, i), "", 0));
			++num_chat_lines;
			s.erase(0, i);
		}
		chatbuffer.push_back(ChatBufferLine(s, "", 0));
	}
	else
		chatbuffer.push_back(ChatBufferLine(s, talker, t));
	++num_chat_lines; // at least one line added
	
	bool forceredraw = false;
	while(num_chat_lines >= CHAT_BUFFER_SIZE)
	{
		chatbuffer.pop_front();
		--chat_top;
	}
	if(chat_top <= 0)
	{
		forceredraw = true;
		chat_top = 0;
	}

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


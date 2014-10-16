// Please see LICENSE file.
#include "msg.h"
#include "base.h"
#include "../common/utfstr.h"
#include "../common/constants.h"
#include "../common/timer.h"
#include "../common/col_codes.h"
#include "../common/classes_common.h"
#include "../common/util.h"
#include <deque>
#include <cstring>

namespace
{
using namespace std;

/*
 * For message window:
 */

// Messages are added (to the screen) at most every STAY_TIME ms.
// Messages are moved away at least every MOVE_FREQ ms.
const unsigned short MOVE_FREQ = 3500; // ms
const unsigned short STAY_TIME = 750;
msTimer last_move, last_add, reference;

// messages on the screen:
struct MsgBufferLine
{
	string text;
	unsigned char cpair;
	char repeat;
	MsgBufferLine(const string &s = "", const unsigned char c = 0)
		: text(s), cpair(c), repeat(0)
	{
		// Make the "normal view coloured" messages lighter initially:
		if(cpair >= C_TREE && cpair < C_TREE_LIT)
			cpair += NUM_NORM_COLS;
	}
	void dimmen() // adjust colour "dimmer"
	{
		if(cpair == 8 || cpair == 0) // already at the dimmest
			return;
		if(cpair > 8 && cpair < BASE_COLOURS)
			cpair -= 8;
		else if(cpair >= C_TREE_LIT)
			cpair -= NUM_NORM_COLS;
		else
			cpair = 8;
	}
};
deque<MsgBufferLine> msgbuffer(MSG_WIN_Y-1); // -1 to leave last row for typing space
char bottom_idx = 0; // y-coord of the first free message slot
// messages awaiting to go onto the screen:
deque<MsgBufferLine> msgavail;

void redraw_msgs()
{
	string tmp_str;
	for(char y = 0; y < MSG_WIN_Y-1; ++y)
	{
		if(msgbuffer[y].repeat)
		{
			tmp_str = msgbuffer[y].text;
			if(num_syms(tmp_str) > MSG_WIN_X-4) // 4: len(" [?]")
				del_syms(tmp_str, MSG_WIN_X-4);
			tmp_str += " [";
			tmp_str += char(msgbuffer[y].repeat + '1');
			tmp_str += ']';
			Base::print_str(tmp_str.c_str(), msgbuffer[y].cpair,
				0, y, MSG_WIN, true);
		}
		else
			Base::print_str(msgbuffer[y].text.c_str(), msgbuffer[y].cpair,
				0, y, MSG_WIN, true);
	}
}

void move_msgs()
{
	if(bottom_idx)
	{
		msgbuffer.pop_front();
		msgbuffer.push_back(MsgBufferLine());
		--bottom_idx;
		last_move.update();
		for(char y = 0; y < bottom_idx/2; ++y)
			msgbuffer[y].dimmen();
	}
}

bool check_repeat_msg(const string &s, const unsigned char cp)
{
	if(bottom_idx && s == msgbuffer[bottom_idx-1].text
		&& cp == msgbuffer[bottom_idx-1].cpair)
	{
		if(msgbuffer[bottom_idx-1].repeat != '+' - '1' // this value is -1
			&& ++(msgbuffer[bottom_idx-1].repeat) > 8)
			msgbuffer[bottom_idx-1].repeat = '+' - '1';
		redraw_msgs();
		last_move.update(); /* only update this! Then the next 
			message can be printed without unnecessary delay */
		return true;
	}
	return false;
}


/*
 * For chat window:
 */
const char CHAT_BUFFER_SIZE = 50;

// cf. e_Team im ../common/classes_common.h
const char* team_ind[4] = { " @: ", " @: ", ": ", "" };

struct ChatBufferLine
{
	string text, speaker;
	unsigned char team;
	ChatBufferLine(const string &m = "", const string &s = "",
		const unsigned char t = 0) : text(m), speaker(s), team(t) {}
};
deque<ChatBufferLine> chatbuffer;
short chat_top = 0;
short num_chat_lines = 0;

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

/*
 * For clocks:
 */
unsigned short maptime = 0;
unsigned char spawntime = 0;
msTimer last_second;

void redraw_clocks()
{
	unsigned short mins = maptime/60;
	unsigned short secs = maptime%60;
	string timestr = "";
	if(mins < 10)
		timestr += ' ';
	timestr += lex_cast(mins);
	timestr += ':';
	if(secs < 10)
		timestr += '0';
	timestr += lex_cast(secs);
	Base::print_str(timestr.c_str(), 7, 51, 6, STAT_WIN, true);

	timestr.clear();
	if(spawntime < 10)
		timestr += ' ';
	timestr += lex_cast(spawntime);
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
	/* First check if this new message is identical with the previous (this
	 * happens especially if someone keeps shouting something */
	if(check_repeat_msg(s, cp))
		return;

	// The rest goes for non-repeating messages:

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
		if(check_repeat_msg(msgavail.front().text, msgavail.front().cpair))
			msgavail.pop_front();
		else
		{
			move_msgs();
			msgbuffer[bottom_idx] = msgavail.front();
			msgavail.pop_front();
			++bottom_idx;
			last_add.update();
			redraw_msgs();
		}
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


#if 0 // (not used)
void clear_msgs()
{
	for(char i = 0; i < bottom_idx; ++i)
	{
		msgbuffer[i].text.clear();
		msgbuffer[i].cpair = 0;
	}
	bottom_idx = 0;
	redraw_msgs();
}
#endif


void add_to_chat(string &s, const string &talker, const unsigned char t)
{
	// Chop into pieces if line is too long:
	unsigned short len = num_syms(talker) + strlen(team_ind[t]);
	short new_lines = 1;
	if(len + num_syms(s) > Base::chat_width())
	{
		int i;
		if((i = s.rfind(' ', Base::chat_width() - len)) == string::npos)
			i = Base::chat_width() - len;
		chatbuffer.push_back(ChatBufferLine(s.substr(0, i), talker, t));
		++new_lines;
		s.erase(0, i);
		while(num_syms(s) > Base::chat_width())
		{
			if((i = s.rfind(' ', Base::chat_width())) == string::npos)
				i = Base::chat_width();
			chatbuffer.push_back(ChatBufferLine(s.substr(0, i), "", T_NO_TEAM));
			++new_lines;
			s.erase(0, i);
		}
		chatbuffer.push_back(ChatBufferLine(s, "", T_NO_TEAM));
	}
	else
		chatbuffer.push_back(ChatBufferLine(s, talker, t));
	num_chat_lines += new_lines;
	
	bool forceredraw = false;
	while(num_chat_lines >= CHAT_BUFFER_SIZE)
	{
		chatbuffer.pop_front();
		--num_chat_lines;
		--chat_top;
	}
	if(chat_top <= 0)
	{
		chat_top = 0;
		forceredraw = true;
	}

	// auto-scrolling:
	if(chat_top + Base::num_chat_lines_to_show() + new_lines == num_chat_lines)
	{
		chat_top += new_lines;
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


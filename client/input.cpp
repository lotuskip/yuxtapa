// Please see LICENSE file.
#include "input.h"
#include "settings.h"
#include "view.h"
#include "base.h"
#include "network.h"
#include "msg.h"
#include "class_cpv.h"
#include "../common/utfstr.h"
#include <ncurses.h>
#include <cctype>
#include <vector>

e_ClientState clientstate = CS_NORMAL;
bool walkmode = false;
std::vector<std::string> prev_strs;

namespace
{
using namespace std;

/* These decimal values of the various special keys I've obtained through
 * testing. Hence, they might not work on some platforms. If it turns out
 * they don't, these constants should be defined separately for those
 * platforms (using precompiler #if .. #else if ... #else ... #endif) */
const char KEYCODE_ENTER = 13; // != '\n'

const char* offon[2] = { "off.]", "on.]" };

vector<string>::const_iterator prev_str_it;
string typed_str, viewn_str;
short type_pos; // in *UTF-8 symbols*, not chars
short str_syms;
short printb_pos;

e_Dir last_dir = MAX_D;

void leave_limbo()
{
	clientstate = CS_NORMAL;
	redraw_view();
	Base::def_cursor();
}

void walkmode_off()
{
	if(walkmode)
		Base::print_walk((walkmode = false));
	last_dir = MAX_D;
}

void init_type()
{
	typed_str.clear();
	viewn_str.clear();
	Base::type_cursor((type_pos = str_syms = 0));
	printb_pos = 0;
	prev_str_it = prev_strs.end();
}

void typing_done()
{
	if(!viewn_str.empty())
	{
		if(prev_strs.empty() || viewn_str != prev_strs.back())
		{
			prev_strs.push_back(viewn_str);
			if(prev_strs.size() > 1000)
				prev_strs.erase(prev_strs.begin());
		}
		Config::do_aliasing(viewn_str);
		Network::send_line(viewn_str, clientstate == CS_TYPE_CHAT);
	}
	Base::print_str("", 0, 0, MSG_WIN_Y-1, MSG_WIN, true);
	Base::def_cursor();
}

void set_viewn_str(const string &s)
{
	type_pos = str_syms = num_syms((viewn_str = s));
	printb_pos = max(0, str_syms - MSG_WIN_X + 1);
}

void loop_up()
{
	if(prev_str_it == prev_strs.begin())
	{
		set_viewn_str(typed_str);
		prev_str_it = prev_strs.end();
	}
	else
	{
		while(prev_str_it != prev_strs.begin())
		{
			if((*(--prev_str_it)).find(typed_str) == 0)
			{
				set_viewn_str(*prev_str_it);
				return;
			}
		}
		// if here, no matches found; recursive call to reset:
		loop_up();
	}
}

void loop_down()
{
	if(prev_strs.empty())
		return;
	if(prev_str_it == prev_strs.end())
		prev_str_it = prev_strs.begin();
	else
		++prev_str_it;
	while(prev_str_it != prev_strs.end())
	{
		if((*prev_str_it).find(typed_str) == 0)
		{
			set_viewn_str(*prev_str_it);
			return;
		}
		++prev_str_it;
	}
	// if here, no matches found:
	set_viewn_str(typed_str);	
}

bool check_typing()
{
	int k;
	wint_t key;

	if((k = get_wch(&key)) == KEY_CODE_YES)
	{
		switch(key)
		{
		/* In my experience, KEY_ENTER never works. But here it is in case
		 * it might, on some obscure platform. */
		case KEY_ENTER:
			typing_done();
			return true;
		// The rest appear to work fine:
		case KEY_BACKSPACE:
			if(type_pos > 0)
			{
				del(viewn_str, type_pos-1);
				typed_str = viewn_str;
				prev_str_it = prev_strs.end();
				if(--type_pos < printb_pos)
					printb_pos = max(0, printb_pos - MSG_WIN_X/2);
				--str_syms;
			}
			break;
		case KEY_DC: // delete key
			if(type_pos < str_syms)
			{
				del(viewn_str, type_pos);
				typed_str = viewn_str;
				prev_str_it = prev_strs.end();
				--str_syms;
			}
			break;
		case KEY_LEFT:
			if(type_pos > 0)
			{
				if(--type_pos < printb_pos)
					printb_pos = max(0, printb_pos - MSG_WIN_X/2);
			}
			break;
		case KEY_RIGHT:
			if(type_pos < str_syms)
			{
				if(++type_pos >= MSG_WIN_X)
					++printb_pos;
			}
			break;
		case KEY_HOME:
			type_pos = printb_pos = 0;
			break;
		case KEY_END:
			type_pos = str_syms;
			printb_pos = max(0, str_syms - MSG_WIN_X + 1);
			break;
		case KEY_UP:
			loop_up();
			break;
		case KEY_DOWN:
			loop_down();
			break;
		case KEY_NPAGE: // pageDown
			prev_str_it = prev_strs.end();
			if(viewn_str != typed_str)
				set_viewn_str(typed_str);
			else
			{
				set_viewn_str("");
				typed_str.clear();
			}	
			break;
		default: // unknown control key
			return false;
		}
	} // control key
	else if(k == OK) // key holds a proper wide character
	{
		if(key == KEYCODE_ENTER)
		{
			typing_done();
			return true;
		}
		// else
		if(key == 27 || key == '\t' || key == KEYCODE_INT) // escape, tab, interrupt
		{
			viewn_str.clear();
			typing_done();
			return true;
		}
		// else
		ins(viewn_str, key, type_pos);
		typed_str = viewn_str;
		prev_str_it = prev_strs.end();
		++str_syms; // string got longer
		++type_pos;
		if(type_pos == str_syms && type_pos >= MSG_WIN_X)
			++printb_pos;
	}
	else // no key at all
		return false;

	// color by chat/say:
	k = (clientstate == CS_TYPE_CHAT) ? 7 : 11;
	Base::print_str(mb_substr(viewn_str, printb_pos,
		min(str_syms - printb_pos, int(MSG_WIN_X))),
		k, 0, MSG_WIN_Y-1, MSG_WIN, true);
	Base::type_cursor(type_pos - printb_pos);
	return false;
}

} // end local namespace


bool Input::inputhandle()
{
	if(clientstate == CS_TYPE_CHAT || clientstate == CS_TYPE_SHOUT)
	{
		if(check_typing()) // returns true if done typing
		{
			clientstate = CS_NORMAL;
			Base::def_cursor();
		}
		return false;
	}
	// else
	
	int key = getch();
	if(key != ERR) // there is some key
	{
		if(key == KEYCODE_INT)
			return true; // ^C quits

		if(clientstate == CS_HELP)
		{
			// any key in help screen closes help
			clientstate = CS_NORMAL;
			leave_limbo(); // not leaving limbo, but the functionality is the same
			return false;
		}

		// Quickshouts handled regardless of clientstate:
		if(ClassCPV::im_alive())
		{
			for(char f = 1; f <= MAX_QUICK_SHOUTS; ++f)
			{
				if(key == KEY_F(f))
				{
					if(!Config::quick_shout(--f).empty())
						Network::send_line(Config::quick_shout(f), false);
					return false;
				}
			}
		}

		if(clientstate == CS_LIMBO)
		{
			key = tolower(key);
			if(key <= 'k' && key >= 'a') // switch class
			{
				Network::send_spawn((unsigned char)(key - 'a'));
				leave_limbo();
			}
			else if(key == 't')
			{
				Network::send_switch();
				leave_limbo();
			}
			else if(key == 's')
			{
				Network::send_spawn(NO_CLASS);
				leave_limbo();
			}
			else if(key == 'l' || key == 27) // 27: escape
				leave_limbo();
		}
		else
		{
			if(key == '?') // help key
			{
				clientstate = CS_HELP;
				walkmode_off();
				redraw_view();
				Base::type_cursor(0);
				return false;
			}

			// if here, we need to convert the key:
			e_Key_binding kb = MAX_KEY_BINDING;
			if(key < 256) // fits in a byte; try to convert
				kb = Config::convert_key(char(key));
			// map arrow keys to the directions, for the n00bs:
			else if(key == KEY_UP)
				kb = KB_8;
			else if(key == KEY_DOWN)
				kb = KB_2;
			else if(key == KEY_LEFT)
				kb = KB_4;
			else if(key == KEY_RIGHT)
				kb = KB_6;
		
			if(kb < MAX_KEY_BINDING) // a recognized key
			{
			switch(kb)
			{
			/*
			 * The effect of these depends on clientstate & class.
			 * The handling is delegated to class_cpv.cpp
			 */
			case KB_1: // movement
			case KB_2: case KB_3: case KB_4: case KB_6: case KB_7: case KB_8: case KB_9:
				ClassCPV::move((last_dir = e_Dir(kb)));
				break;
			case KB_5: // special direction
				ClassCPV::five();
				last_dir = MAX_D;
				break;
			case KB_SPACE: // action key
				ClassCPV::space();
				walkmode_off();
				break;
			case KB_p: // follow previous key
				ClassCPV::follow_prev();
				walkmode_off();
				break;

			/*
			 * These can be done in the middle of unfinished actions, but will cause
			 * interruption:
			 */
			case KB_C: // say in chat
				clientstate = CS_TYPE_CHAT;
				init_type();
				walkmode_off();
				break;
			case KB_s: // say aloud
				clientstate = CS_TYPE_SHOUT;
				init_type();
				walkmode_off();
				break;
			case KB_c: // close a door
				Network::send_action(XN_CLOSE_DOOR);
				// interrupts any aiming/casting etc:
				clientstate = CS_NORMAL;
				walkmode_off();
				break;
			case KB_u: // torch handling
				Network::send_action(XN_TORCH_HANDLE);
				// interrupts any aiming/casting etc:
				clientstate = CS_NORMAL;
				walkmode_off();
				break;
			case KB_T: // trigger a trap
				Network::send_action(XN_TRAP_TRIGGER);
				// interrupts any aiming/casting etc:
				clientstate = CS_NORMAL;
				walkmode_off();
				break;
			case KB_X: // suicide
				ClassCPV::suicide();
				walkmode_off();
				break;
			case KB_l: // limbo toggle
				clientstate = CS_LIMBO;
				walkmode_off();
				redraw_view();
				Base::type_cursor(0);
				break;
			case KB_Q: // quit
				return true; // confirmation? nah

			/*
			 * The following can be carried out in any clientstate except limbo
			 * without interruption:
			 */
			case KB_w: // walk toggle
				if(walkmode)
					walkmode_off();
				else
					Base::print_walk((walkmode = true));
				break;
			case KB_t: // titles toggle
				toggle_titles();
				break;
			case KB_d: // differentiate titles
				diff_titles();
				break;
			case KB_o: // ouching toggle
				add_msg(string("[Reacting to damage ") + offon[Config::toggle_ouch()], 15);
				break;
			case KB_PLUS:
				scroll_chat_up();
				break;
			case KB_MINUS:
				scroll_chat_down();
				break;
			case KB_PERCENT:
				add_msg(string("[Act per turn ") + offon[Config::toggle_act_per_turn()], 15);
				break;
			default: break;
			}
			} // a recognized key binding
		} // need to convert key
	} // key != ERR
	else if(clientstate == CS_NORMAL && walkmode && last_dir != MAX_D
		&& Network::not_acted())
		ClassCPV::move(last_dir);
	return false;
}

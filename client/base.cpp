// Please see LICENSE file.
#include "base.h"
#include "settings.h"
#include "../common/utfstr.h"
#include "../common/util.h"
#include <ncurses.h>
#include <iostream>
#include <string>
#include <vector>
#include "../common/constants.h"
#include "../common/coords.h"
#include "colourdef.h"

namespace
{
using namespace std;

const char MIN_SCREEN_X = 80;
const char MIN_SCREEN_Y = 24;

const char OUR_ESC_DELAY = 20; //ms

const char walk_syms[2] = { ' ', 'W' };

// These match to e_Dir values:
const char flagy[9] = { 4, 4, 5, 6, 6, 6, 5, 4, 5 };
const char flagx[9] = { 31, 33, 33, 33, 31, 29, 29, 29, 31 };

short scr_x, scr_y;
enum e_ColorMode { CM_FULL=0, //full colours that can be changed
	CM_MANY, //full, immutable colours
	CM_FEW }; // 8+8 colour mode (xterm)
e_ColorMode colmode;

short *term_colours;
WINDOW *windows[MAX_WIN]; // STAT_WIN, CHAT_WIN, MSG_WIN, VIEW_WIN

unsigned char animind = 0;
vector<string> anim_syms; // since the symbols can be UTF_8, they might be multibyte!

bool cursor_in_view = true;
char cursor_x; // when typing

// return cursor to where-ever it is supposed to be
void return_cursor(const bool def)
{
	if(def) // cursor in the view
	{
		wmove(windows[VIEW_WIN], VIEWSIZE/2, VIEWSIZE/2);
		wrefresh(windows[VIEW_WIN]);
	}
	else
	{
		wmove(windows[MSG_WIN], MSG_WIN_Y-1, cursor_x);
		wrefresh(windows[MSG_WIN]);
	}
}

void change_colour(WINDOW* const w, unsigned char cpair)
{
	if(colmode != CM_FEW)
		wattrset(w, COLOR_PAIR(cpair));
	else
	{
		// Reduce the colours to the base 16:
		if(cpair >= BASE_COLOURS)
		{
			if(cpair < C_BG_HEAL)
				cpair = col_remap[cpair - BASE_COLOURS]; // (and goto set_colour)
			else // background is not black
			{
				// check the cases that require light foreground:
				if(cpair == C_BROWN_ON_TRAP || cpair == C_YELLOW_ON_TRAP
					|| cpair == C_GREEN_ON_POIS || cpair == C_RED_ON_HIT
					|| cpair == C_BLUE_ON_HEAL)
					wattrset(w, (A_BOLD
						| (COLOR_PAIR(cpair - C_BG_HEAL + BASE_COLOURS))));
				else
					wattrset(w, COLOR_PAIR(cpair - C_BG_HEAL + BASE_COLOURS));
				return;
			}
		}
		// set_colour:
		if(cpair >= BASE_COLOURS/2)
			wattrset(w, (A_BOLD | (COLOR_PAIR(cpair))));
		else
			wattrset(w, COLOR_PAIR(cpair));
	} // only few colours
}

} // end local namespace


bool Base::init_ncurses()
{
	short c;
	// First init the animation:
	string full_anim = Config::get_anim();
	for(c = 0; c < num_syms(full_anim); ++c)
		anim_syms.push_back(sym_at(full_anim, c));

	// Then the actual NCurses stuff
	if(!initscr())
	{
		cout << "Failed to init NCurses screen!" << endl;
		return false;
	}

	getmaxyx(stdscr, scr_y, scr_x);
	if(scr_x < MIN_SCREEN_X || scr_y < MIN_SCREEN_Y)
	{
		endwin();
		cout << "Terminal window is too small! Required minimum is "
			<< int(MIN_SCREEN_X) << 'x' << int(MIN_SCREEN_Y) << '.' << endl;
		return false;
	}

	start_color();
	raw();
	keypad(stdscr, TRUE);
	noecho();
	timeout(GETCH_TIMEOUT);
	set_escdelay(OUR_ESC_DELAY);
	nonl();

	// init base colours first: (with black background)
	char d;
	char ct[BASE_COLOURS] = { COLOR_BLACK, COLOR_RED, COLOR_GREEN,
		COLOR_YELLOW, COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE,
		COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW, COLOR_BLUE,
		COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE };
	for(c = 0; c < BASE_COLOURS; ++c)
		init_pair(c, ct[c], COLOR_BLACK);

	// figure out colour mode:
	if(can_change_color() && COLORS >= MAX_PREDEF_COLOUR)
	{
		colmode = CM_FULL;
		term_colours = new short[3*(MAX_PREDEF_COLOUR-BASE_COLOURS)];
		for(c = 0; c < MAX_PREDEF_COLOUR-BASE_COLOURS; ++c)
		{
			// store old colours:
			color_content(c+BASE_COLOURS, &term_colours[3*c],
				&term_colours[3*c+1], &term_colours[3*c+2]);
			// and replace with ours:
			init_color(c+BASE_COLOURS, colour[c][0], colour[c][1],
				colour[c][2]); 
		}
		// (see common/col_codes.h)
		for(c = BASE_COLOURS; c < C_BG_HEAL; ++c)
			init_pair(c, c, COLOR_BLACK);
		for(d = 0; d < 6; ++d)
		{
			for(c = 0; c < 7; ++c)
				init_pair(C_GREEN_ON_HEAL+d*7+c, C_BASE_GREEN+c, C_BG_HEAL+d);
		}
		init_pair(C_UNKNOWN, C_WALL_DIM, C_WALL_DIM); 
	}
	else if(COLORS >= MAX_PREDEF_COLOUR) // immutable multi-colour
	{
		colmode = CM_MANY;
		for(c = BASE_COLOURS; c < C_BG_HEAL; ++c)
			init_pair(c, fixed_remap[c-BASE_COLOURS], COLOR_BLACK);
		for(d = 0; d < 6; ++d)
		{
			for(c = 0; c < 7; ++c)
				init_pair(C_GREEN_ON_HEAL+d*7+c, fixed_fgc[c], fixed_bgc[d]);
		}
		init_pair(C_UNKNOWN, 8, 8);
	}
	else // only 8+8 colours
	{
		colmode = CM_FEW;
		// Init the pairs with non-black background
		for(d = 0; d < 6; ++d)
		{
			for(c = 0; c < 7; ++c)
				init_pair(BASE_COLOURS+d*7+c, fgc[c], bgc[d]);
		}
		init_pair(BASE_COLOURS+6*7, BASE_LIGHT_GRAY, BASE_LIGHT_GRAY);
	}
	
	// (ysize, xsize, ypos, xpos)
	windows[STAT_WIN] = newwin(VIEWSIZE - MSG_WIN_Y, MIN_SCREEN_X - VIEWSIZE, 0, 0);
	windows[VIEW_WIN] = newwin(VIEWSIZE, VIEWSIZE, 0, MIN_SCREEN_X - VIEWSIZE);
	windows[MSG_WIN] = newwin(MSG_WIN_Y, MSG_WIN_X, VIEWSIZE - MSG_WIN_Y, 0);
	windows[CHAT_WIN] = newwin(scr_y - VIEWSIZE, scr_x, VIEWSIZE, 0);

	refresh();
	flushinp();
	return true;
}


void Base::deinit_ncurses()
{
	int i;
	if(colmode == CM_FULL)
	{
		// restore original colours: (this gives a nice shutdown "animation", too)
		for(i = 0; i < MAX_PREDEF_COLOUR-BASE_COLOURS; ++i)
			init_color(i+BASE_COLOURS, term_colours[3*i], term_colours[3*i+1],
				term_colours[3*i+2]);
		delete[] term_colours;
	}
	for(i = 0; i < MAX_WIN; ++i)
		delwin(windows[i]);
	echo();
	endwin();
}


void Base::print_str(const char *cs, const unsigned char cpair, const char x,
	const char y, const e_Win targetwin, const bool cteol)
{
	wmove(windows[targetwin], y, x);
	change_colour(windows[targetwin], cpair);
	waddstr(windows[targetwin], cs);
	if(cteol)
		wclrtoeol(windows[targetwin]);
	wrefresh(windows[targetwin]);
	return_cursor(cursor_in_view);
}

void Base::incr_print_start(const char x, const char y, const e_Win targetwin)
{
	wmove(windows[targetwin], y, x);
}
void Base::incr_print(const char *cs, const unsigned char cpair, const e_Win targetwin)
{
	change_colour(windows[targetwin], cpair);
	waddstr(windows[targetwin], cs);
}
void Base::incr_print_end(const e_Win targetwin, const bool cteol)
{
	if(cteol)
		wclrtoeol(windows[targetwin]);
	wrefresh(windows[targetwin]);
	return_cursor(cursor_in_view);
}

void Base::more_chat_ind()
{
	wmove(windows[CHAT_WIN], scr_y - VIEWSIZE - 1, scr_x - 4);
	change_colour(windows[CHAT_WIN], BASE_BLUE);
	waddstr(windows[CHAT_WIN], "[+]");
	wrefresh(windows[CHAT_WIN]);
	return_cursor(cursor_in_view);
}

void Base::less_chat_ind()
{
	wmove(windows[CHAT_WIN], 0, scr_x - 4);
	change_colour(windows[CHAT_WIN], BASE_BLUE);
	waddstr(windows[CHAT_WIN], "[-]");
	wrefresh(windows[CHAT_WIN]);
	return_cursor(cursor_in_view);
}

unsigned char Base::num_chat_lines_to_show() { return scr_y - VIEWSIZE; }
short Base::chat_width() { return scr_x; }


void Base::print_view(const char *source)
{
	// The view has a sequence of pairs (colour, symbol):
	char x, y;
	for(y = 0; y < VIEWSIZE; ++y)
	{
		wmove(windows[VIEW_WIN], y, 0);
		for(x = 0; x < VIEWSIZE; ++x)
		{
			change_colour(windows[VIEW_WIN], *source);
			waddch(windows[VIEW_WIN], *(++source));
			++source;
		}
	}
	wrefresh(windows[VIEW_WIN]);
	return_cursor(cursor_in_view);
}


void Base::def_cursor()
{
	return_cursor((cursor_in_view = true));
}

void Base::type_cursor(const char ind)
{
	cursor_x = ind;
	return_cursor((cursor_in_view = false));
}


void Base::viewtick()
{
	wmove(windows[STAT_WIN], 4, 53);
	change_colour(windows[STAT_WIN], C_WALL_LIT);
	waddstr(windows[STAT_WIN], anim_syms[animind].c_str());
	wrefresh(windows[STAT_WIN]);
	if(++animind == anim_syms.size())
		animind = 0;
	return_cursor(cursor_in_view);
}

void Base::print_walk(const bool w)
{
	wmove(windows[STAT_WIN], 4, 55);
	change_colour(windows[STAT_WIN], C_WALL_LIT);
	waddch(windows[STAT_WIN], walk_syms[w]);
	wrefresh(windows[STAT_WIN]);
	return_cursor(cursor_in_view);
}


void Base::print_teams_upd(const unsigned char greens,
	const unsigned char purples, const std::string &obj_state_str)
{
	wmove(windows[STAT_WIN], 0, 0);
	change_colour(windows[STAT_WIN], BASE_LIGHT_GRAY);
	waddstr(windows[STAT_WIN], obj_state_str.c_str());
	wclrtoeol(windows[STAT_WIN]);

	wmove(windows[STAT_WIN], 1, 0);
	change_colour(windows[STAT_WIN], C_GREEN_PC);
	string str = "Green team: "
		+ lex_cast(greens)
		+ " players ";
	waddstr(windows[STAT_WIN], str.c_str());
	change_colour(windows[STAT_WIN], C_PURPLE_PC);
	str = " Purple team: "
		+ lex_cast(purples)
		+ " players";
	waddstr(windows[STAT_WIN], str.c_str());
	wclrtoeol(windows[STAT_WIN]);
	wrefresh(windows[STAT_WIN]);
	return_cursor(cursor_in_view);
}


void Base::print_flags(const string &fs)
{
	// First print the empty box:
	print_str("_______", 7, 28, 3, STAT_WIN);
	print_str("|     |", 7, 28, 4, STAT_WIN);
	print_str("|     |", 7, 28, 5, STAT_WIN);
	print_str("|_____|", 7, 28, 6, STAT_WIN);

	for(char i = 0; i < MAX_D+1; ++i)
	{
		if(fs[i] != MAX_PREDEF_CPAIR)
		{
			wmove(windows[STAT_WIN], flagy[i], flagx[i]);
			change_colour(windows[STAT_WIN], fs[i]);
			waddch(windows[STAT_WIN], '&');
		}
	}
	wrefresh(windows[STAT_WIN]);
}


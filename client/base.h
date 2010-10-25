// Please see LICENSE file.
#ifndef BASE_H
#define BASE_H

#include <deque>
#include <string>

enum e_Win {
	STAT_WIN=0, // status window contains server, team & objective info
	CHAT_WIN, // chat window has chat messages
	MSG_WIN, // message window has messages and typing space
	PC_WIN, // PC window has PC information and some indicators
	VIEW_WIN, // view window has the game view
	MAX_WIN };

const char KEYCODE_INT = 3; // ^C

const char GETCH_TIMEOUT = 40; /* ms to wait for keyboard input upon a getch()
	call. Might have to experiment with this. Zero is ridiculous (makes the
	client a CPU hog), and too high might make it respond too slow. */

namespace Base
{
	bool init_ncurses();
	void deinit_ncurses();

	// Print a single colour string in given window, at (x,y)
	void print_str(const char *cs, const unsigned char cpair, const char x,
		const char y, const e_Win targetwin, const bool cteol = false);

	// to print several strings of different colours after each other:
	void incr_print_start(const char x, const char y, const e_Win targetwin);
	void incr_print(const char *cs, const unsigned char cpair, const e_Win targetwin);
	void incr_print_end(const e_Win targetwin, const bool cteol);

	void redraw_msgs(const std::deque<std::string> &msgs,
		const std::deque<unsigned char> &cpairs);
	char num_chat_lines_to_show();
	void more_chat_ind(); // print indicators of there being more/less chat msgs
	void less_chat_ind();

	void print_teams_upd(const unsigned char greens, const unsigned char purples,
		const std::string &obj_state_str);

	// Do a full view update, data at 'source'
	void print_view(const char *source);
	void viewtick(); // updates the view update animation
	void print_walk(const bool w); // walkmode indicator

	// Moving the cursor; the cursor is always in the view or in the typing space:
	void def_cursor();
	void type_cursor(const char ind);
}

#endif

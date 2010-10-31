// Please see LICENSE file.
#ifndef VIEW_H
#define VIEW_H

// these affect both what kind of input is accepted (see input.cpp)
// and what is drawn on the screen.
enum e_ClientState {
	CS_NORMAL=0, // normal input-receiving state
	CS_TYPE_CHAT, // typing a chat msg
	CS_TYPE_SHOUT, // typing a shout
	CS_LIMBO, // viewing the limbo menu
	CS_HELP, // viewing the keymap
	// class specifics:
	CS_AIMING, // archer aiming
	CS_DIR // waiting for direction input
};

void toggle_titles();
void redraw_view();

#endif

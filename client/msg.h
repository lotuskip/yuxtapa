// Please see LICENSE file.
#ifndef MSG_H
#define MSG_H

#include <string>

// NOTE: the cpair passed to add_msg should NOT be any _DIM colour, nor higher
// than the last _LIT colour! This ensures the "dimming" of older messages works
// correctly.
void init_msgs();
void add_msg(const std::string &s, const unsigned char cp);
void upd_msgs();
#if 0
void clear_msgs(); // clears all messages, resulting in an empty buffer
#endif

void add_to_chat(std::string &s, const std::string &talker,
	const unsigned char t);
void scroll_chat_up();
void scroll_chat_down();

void clocksync(const unsigned short t, const unsigned char spawn);

#endif

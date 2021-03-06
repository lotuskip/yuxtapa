// Please see LICENSE file.
#ifndef CLIENT_SETTINGS_H
#define CLIENT_SETTINGS_H

#include <string>

enum e_Key_binding {
	// NOTE: the order of these matches the directions in ../common/coords.h!
	KB_8 = 0, KB_9, KB_6, KB_3, KB_2, KB_1, KB_4, KB_7,
	KB_5, KB_SPACE, KB_p, KB_C, KB_s, KB_c, KB_T, KB_X, KB_u, KB_w, KB_t,
	KB_d, KB_PLUS, KB_MINUS, KB_l, KB_Q, KB_o, KB_i, KB_PERCENT,
	MAX_KEY_BINDING };

const char MAX_QUICK_SHOUTS = 12;

namespace Config
{
	void read_config(const std::string &servername);
	void save_msg_memory();

	std::string &get_server_ip();
	std::string &get_nick();
	std::string &get_anim();
	std::string &quick_shout(const char index);
	std::string &get_ouch();
	bool do_ouch();
	bool toggle_ouch();
	bool act_per_turn();
	bool toggle_act_per_turn();

	void do_aliasing(std::string &s);

	// give the internal key where the character is mapped to
	e_Key_binding convert_key(const char key);
	// Returns the char (possibly "^X") which is mapped to kb:
	char* key_map_value(const e_Key_binding kb);

	std::string &configdir();
}

#endif

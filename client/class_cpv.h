// Please see LICENSE file.
#ifndef CLASS_CPV_H
#define CLASS_CPV_H

#include "../common/classes_common.h"
#include "../common/coords.h"
#include "../common/netutils.h"

namespace ClassCPV
{
	void move(const e_Dir d);
	void five();
	void space();
	void follow_prev();
	void suicide();

	// received new class and team:
	void state_change(const unsigned char cl, const unsigned char t);
	// received new hp/2hit/dam:
	void state_upd(SerialBuffer &data);

	bool im_alive();
	unsigned char get_cur_team_col();
}

#endif

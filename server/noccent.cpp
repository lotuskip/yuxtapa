// Please see LICENSE file.
#include "noccent.h"
#include "map.h"

namespace Game { extern Map* curmap; }

namespace
{
// trap flags:
enum { SEEN_BY_GREEN=0x01, SEEN_BY_PURPLE=0x02 };

}

NOccEnt::NOccEnt(const Coords &c, const char sym, const char cp,
	const unsigned char m)
	: Ent(c, sym, cp), misc(m)
{
}


void NOccEnt::update()
{
	// At the moment this gets called only for corpses!
	if(!(--misc))
	{
		makevoid();
		Game::curmap->mod_tile(pos)->flags &= ~(TF_NOCCENT);
	}
}


Trap::Trap(const Coords &c, const unsigned char m,
	const std::list<Player>::iterator o)
	: NOccEnt(c, '^', 0, m), flags(0), owner(o)
{
	owner = cur_players.end();
	set_col(C_WATER_TRAP + m);
}


void Trap::set_seen_by(const unsigned char t)
{
	flags |= SEEN_BY_GREEN + char(t == T_PURPLE);
}

bool Trap::is_seen_by(const unsigned char t) const
{
	return flags & (SEEN_BY_GREEN + char(t == T_PURPLE));
}


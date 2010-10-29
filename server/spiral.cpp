// Please see LICENSE file.
#ifndef MAPTEST
#include "spiral.h"

namespace
{
const char coord_adds[4] = { 0, 1, 0, -1 };

Coords pnt;
short elen, lenidx, eind;
short maxdim;
}

void nearby_set_dim(const short mapsize) { maxdim = mapsize; }

void init_nearby(const Coords &c)
{
	pnt = c; elen = 1; lenidx = 0, eind = 1;
}

Coords next_nearby()
{
	do {
		pnt.x += coord_adds[eind % 4];
		pnt.y += coord_adds[(eind-1)%4];
		if(++lenidx == elen)
		{
			if((++eind)%2)
				++elen;
			lenidx = 0;
		}
	}
	while(pnt.x < 1 || pnt.y < 1 || pnt.x >= maxdim
		|| pnt.y >= maxdim);
	return pnt;
}

#endif // not maptest build

//Please see LICENSE file.
#include "coords.h"
#include <cmath>
#include <cstdlib>

namespace
{

const e_Dir opposites[MAX_D] = {
D_S, // D_N
D_SW, // D_NE
D_W, // D_E
D_NW, // D_SE
D_N, // D_S
D_NE, // D_SW
D_E, // D_W
D_SE // D_NW
};

} // end local namespace


e_Dir& operator++(e_Dir& d) { return d = (D_NW==d) ? D_N : e_Dir(d+1); }
e_Dir& operator--(e_Dir& d) { return d = (D_N==d) ? D_NW : e_Dir(d-1); }

e_Dir operator!(const e_Dir d)
{
	//assert(d != MAX_D);
	return opposites[d];
}

Coords::Coords(const short nx, const short ny) : x(nx), y(ny) { }

Coords::~Coords() {}


float Coords::dist_eucl(const Coords &from_where) const
{
	return sqrt(pow(double(x - from_where.x),2) + pow(double(y - from_where.y),2));
}

#if 0
short Coords::dist_taxicab(const Coords &from_where) const
{
	return std::abs(x - from_where.x) + std::abs(y - from_where.y);
}
#endif

short Coords::dist_walk(const Coords &from_where) const
{
	return std::max(std::abs(x - from_where.x), std::abs(y - from_where.y));
}

// coordinates of neighboring location according to direction
Coords Coords::in(const e_Dir d) const
{
	Coords c(x,y);
	switch(d)
	{
		case D_N: c.y--; break;
		case D_NE: c.x++; c.y--; break;
		case D_E: c.x++; break;
		case D_SE: c.x++; c.y++; break;
		case D_S: c.y++; break;
		case D_SW: c.x--; c.y++; break;
		case D_W: c.x--; break;
		case D_NW: c.x--; c.y--; break;
		default: /*assert(0)*/;
	}
	return c;
}

void Coords::unitise()
{
	short len = short(dist_eucl(Coords(0,0)));
	if(len)
	{
		x /= len;
		y /= len;
	}
}

e_Dir Coords::dir_of(Coords ref) const
{
	// Basically, we construct the vector from *this to ref and
	// shrink it (in a Euclidian fashion) to a unit vector, which
	// we then interpret as a direction:
	ref.x -= x;
	ref.y -= y;
	ref.unitise();
	switch(ref.x + ref.y)
	{
	case 2: return D_SE;
	case -2: return D_NW;
	case 1: if(ref.x) return D_E;
		return D_S;
	case -1: if(ref.x) return D_W;
		return D_N;
	case 0: if(ref.x == 1) return D_NE;
		return D_SW;
	}
	return D_N; // reaching this line is an error!
}


bool Coords::operator<(const Coords &b) const
{
	if(x != b.x)
		return x < b.x; // 1. by x-coordinate
	return y < b.y; // 2. by y-coordinate
	// both coords can't be the same, so this really is an full order-relation for the coordinates!
}


bool Coords::operator==(const Coords &b) const
{
	return (x == b.x && y == b.y);
}

#if 0
Coords& Coords::swap()
{
	short tmp = x;
	x = y;
	y = tmp;
	return *this;
}

Coords& Coords::vary(const short max_dist)
{
	x += rand() % (2*max_dist) - max_dist;
	y += rand() % (2*max_dist) - max_dist;
	return *this;
}
#endif


#if 0
void interpolate(Coords s, Coords e, std::vector<Coords> &res)
{
	bool steep = abs(e.y - s.y) > abs(e.x - s.x);
	if(steep)
	{
		swap(s.x, s.y);
		swap(e.x, e.y);
	}
	if(s.x > e.x)
	{
		swap(s.x, e.x);
		swap(s.y, e.y);
	}
	int deltax = e.x - s.x;
	int deltay = abs(e.y - s.y);
	int error = deltax/2;
	int ystep;
	int y = s.y;
	if(s.y < e.y)
		ystep = 1;
	else
		ystep = -1;
	for(int x = s.x; x <= e.x; ++x)
	{
		if(steep)
			res.push_back(Coords(y,x));
		else
			res.push_back(Coords(x,y));
		if((error -= deltay) < 0)
		{
			y += ystep;
			error += deltax;
		}
	}
}
#endif


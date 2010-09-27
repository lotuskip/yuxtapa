//Please see LICENSE file.
#ifndef COORDS_H
#define COORDS_H

#include <vector>
#include <string>

enum e_Dir { D_N = 0, D_NE, D_E, D_SE, D_S, D_SW, D_W, D_NW, MAX_D };
e_Dir& operator++(e_Dir& d); // "turns direction 45deg clockwise"
e_Dir& operator--(e_Dir& d); // "turns direction 45deg anticlockwise"
e_Dir operator!(const e_Dir d); // gives opposite direction

const std::string sector_name[MAX_D+1] = { "N", "NE", "E", "SE", "S",
	"SW", "W", "NW", "C" };

class Coords
{
public:
	Coords(const short nx = 0, const short ny = 0);
	~Coords();

	short x, y;
	// euclidian, taxicab, and walking distances:
	float dist_eucl(const Coords &from_where) const;
#if 0
	// taxicab not needed
	short dist_taxicab(const Coords &from_where) const;
#endif
	short dist_walk(const Coords &from_where) const;

	// directions interpreted as neighboring coordinates
	Coords in(const e_Dir d) const;
	
	void unitise(); // turn into a "unit vector"
	
	// Gives the direction in which (roughly) the given coords are from *this
	e_Dir dir_of(Coords ref) const;

	// for sorting and searching in STL containers:
	bool operator<(const Coords &b) const;
	bool operator==(const Coords &b) const;
	
#if 0
	Coords& swap(); // swaps x and y and return *this
	// modify the coordinates randomly, not more than [max_dist] away (walk metric)
	Coords& vary(const short max_dist);
#endif
};

#if 0
// Bresenham's algorithm to get points in between two points:
void interpolate(Coords s, Coords e, std::vector<Coords> &res);
#endif

#endif

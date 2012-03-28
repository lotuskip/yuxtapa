// Please see LICENSE file.
#ifndef MAP_H
#define MAP_H

#include "declares.h"
#include "../common/coords.h"
#include <vector>
#include <utility>

struct Tile
{
	unsigned short flags;
	char symbol;
	unsigned char cpair; // colour pair index
};
// the flags; many of these are only needed for optimisation
enum {
	TF_WALKTHRU=0x0001, // can be walked on
	TF_SLOWWALK=0x0002, // swamp & rough & water
	TF_SEETHRU=0x0004, // light goes through
	TF_BYWALL=0x0008, // quicker to check than going through neighbours
	TF_LIT=0x0010, // permanently lit (there's dynamical lighting separately)
	TF_NOCCENT=0x0020, // fast indicator of some non-occupying entities
	TF_OCCUPIED=0x0040, // fast indicator that some dynamic entity is here
	TF_ACTION=0x0080, // fast ind. that there's an action indicator pointing here
	TF_SOUND=0x0100, // fast ind. of a sound event
	TF_KILLS=0x0200, // chasm tile
	TF_DROWNS=0x0400, // water tile
	TF_TRAP=0x0800, // trapped tile
	TF_NODIG=0x1000 // cannot be dug (for walls)
};
// The empty tile is what is drawn for unknown places.
const Tile EMPTY_TILE = { TF_SEETHRU, '?', C_UNKNOWN };
const Tile T_FLOOR = { TF_WALKTHRU|TF_SEETHRU,
#ifdef MAPTEST
'_'
#else
'.'
#endif
, C_FLOOR };

/* These need to match the bounds obtained from what settings accepts
 * as base map size and map size variation! */
const short MIN_MAP_SIZE = 42;
const short MAX_MAP_SIZE = 511;

class Map
{
public:
	// generates a new random map
	Map(const short size, const short variation, const short players);
	~Map();

	bool load_from_file(const std::string &mapname);
	bool save_to_file(const std::string &mapname) const;

	void gen_miniview(char *target) const;

	short get_size() const { return mapsize; }
	Coords get_center_of_sector(const e_Dir d) const;
	e_Dir coords_in_sector(const Coords &c) const;

#if 0
	void get_copy(std::vector< std::vector<Tile> > &res) const; // copy entire map
#endif
	void make_right_size(std::vector< std::vector<Tile> > &res) const;

	Tile get_tile(const Coords &c) const // copy one tile
		{ return data[c.y][c.x]; }
	Tile* mod_tile(const short x, const short y) { return &(data[y][x]); }
	Tile* mod_tile(const Coords &c) { return &(data[c.y][c.x]); }
	bool point_out_of_map(const Coords &c) const;
	
	bool LOS_between(const Coords &c1, const Coords &c2, const char maxrad,
		const short calced_rad = -1);

	bool is_inhabited() const { return inhabited; }
	bool is_outdoor() const { return maptype == MT_OUTDOOR; }

	// When walls are removed (by digging), call this an all neighbours
	// to update their being next to wall or not:
	void upd_by_wall_flag(const Coords &c);

private:
	short mapsize;
	bool inhabited;
	e_MapType maptype;

	// the map, mapsize*mapsize square
	std::vector< std::vector<Tile> > data;

	void apply_house(const Coords &c);
	void add_patch(const Coords &c, const Tile &t);
	void fix_boundary();
	
	// This is used by miniview
	Tile subsample_tile(const Tile &t) const;
};


#endif

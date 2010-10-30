// Please see LICENSE file.
#include "../config.h"
#include "map.h"
#include "settings.h"
#include "../common/util.h"
#ifndef MAPTEST
#include "log.h"
#include "../common/constants.h"
#include "../common/los_lookup.h"
#include <boost/lexical_cast.hpp>
#endif
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <algorithm>
#include <map>
#include <vector>
#include <fstream>
#if defined MAPTEST || defined DEBUG
#include <iostream>
#endif

namespace
{
using namespace std;

const char CHANCE_ROUGH = 22; // how much of the floor in dungeons is rough, %
const char EVERY_N_WALL_UNDIG[2] = { // every Nth wall is made undiggable (in random)
	30, // dungeon
	12 // outdoor
};

// to reject maps saved with older specifications
const unsigned char MAP_STORE_VERSION = 1;

// The tile flags that are static and relevant in a "raw map" that gets saved
// to a file:
const unsigned short STATIC_TF =
	TF_WALKTHRU|TF_SLOWWALK|TF_SEETHRU|TF_BYWALL|TF_KILLS|TF_DROWNS|TF_NODIG;

const Tile T_TREE = { TF_WALKTHRU|TF_SEETHRU, 'T', C_TREE };
const Tile T_GROUND = { TF_WALKTHRU|TF_SEETHRU, '.', C_GRASS };
const Tile T_WALL = { 0, '#', C_WALL };
const Tile T_CHASM = { TF_WALKTHRU|TF_SEETHRU|TF_KILLS, ' ', C_FLOOR };
const Tile T_SWAMP = { TF_WALKTHRU|TF_SLOWWALK|TF_SEETHRU, '\"', C_TREE };
const Tile T_ROUGH = { TF_WALKTHRU|TF_SLOWWALK|TF_SEETHRU, ';', C_WALL };
const Tile T_WATER = { TF_WALKTHRU|TF_SEETHRU|TF_DROWNS, '~', C_WATER };
const Tile T_ROAD = { TF_WALKTHRU|TF_SEETHRU,
#ifdef MAPTEST
','
#else
'.'
#endif
, C_ROAD };
const Tile T_DOOR = { 0, '+', C_DOOR };
const Tile T_WINDOW = { TF_SEETHRU, '|', C_WALL };

typedef vector< vector<Tile> >::iterator rowit;

bool operator!=(const Tile &lhs, const Tile &rhs)
{
	return lhs.symbol != rhs.symbol || lhs.cpair != rhs.cpair;
}

bool operator==(const Tile &lhs, const Tile &rhs)
{
	return !(lhs != rhs);
}

#ifndef MAPTEST
//needed in minimap generation:
bool pred(const pair<Tile, short> &lhs, const pair<Tile, short> &rhs)
{
	return lhs.second < rhs.second;
}
#endif

///////////////////////////////////////////////////////////////////
/*
 * This part of the generator is based on code written by Paul E. Martz, who
 * has kindly stated that non-commercial use by individuals is permitted.
 */

// returns a float between -v and v
float fractRand(const float v)
{
    float x = float(random() & 0x7fff)/(float)0x7fff;
    return (x*2.0f - 1.0f)*v;
}

float avgDiamondVals(const int i, const int j, const int stride,
	const int size, const int subSize, const float *fa)
{
    if(!i)
		return (fa[(i*size) + j-stride] + fa[(i*size) + j+stride]
		+ fa[((subSize-stride)*size) + j] + fa[((i+stride)*size) + j])*.25f;
    if(i == size-1)
		return (fa[(i*size) + j-stride] + fa[(i*size) + j+stride]
		+ fa[((i-stride)*size) + j] + fa[((0+stride)*size) + j])*.25f;
    if(!j)
		return (fa[((i-stride)*size) + j] + fa[((i+stride)*size) + j]
		+ fa[(i*size) + j+stride] +	 fa[(i*size) + subSize-stride])*.25f;
    if(j == size-1)
		return (fa[((i-stride)*size) + j] + fa[((i+stride)*size) + j]
		+ fa[(i*size) + j-stride] + fa[(i*size) + 0+stride])*.25f;
    return float(fa[((i-stride)*size) + j] + fa[((i+stride)*size) + j]
		+ fa[(i*size) + j-stride] + fa[(i*size) + j+stride])*.25f;
}

float avgSquareVals(const int i, const int j, const int stride, const int size,
	const float *fa)
{
    return (fa[((i-stride)*size) + j-stride] + fa[((i-stride)*size) + j+stride]
		+ fa[((i+stride)*size) + j-stride] + fa[((i+stride)*size) + j+stride])*.25f;
}

/*
 * Size must be a power of two + 1 (65, 129, 257, 513)
 * h determines smoothness: 0->very jagged, 1->very smooth
 */
void fill2DFractArray(float *fa, const int size, const float h)
{
    int subSize = size-1;
	float ratio = pow(2.,-h);
	float scale = ratio;
    int stride = subSize/2;

	// Initialize the corner values. This greatly affects the final outcome,
	// so we don't just randomize them (that can lead to maps that all almost
	// completely flooded or contain almost solely rock (which, admittedly,
	// still happens now but less often! I have not been able to find fixed
	// seeds that would (a) yield interesting maps, and (b) never result in
	// stupid maps.))
	vector<float> seeds(4);
	seeds[0] = 0.75f + fractRand(0.25f); // (0.5,1.0)
	seeds[1] = fractRand(0.5f); // (-0.5,0.5)
	seeds[2] = fractRand(0.5f); // (-0.5,0.5)
	seeds[3] = -0.75f + fractRand(0.25f); //(-1.0,-0.5)

	// Assign those randomly to the four corners:
	vector<float>::iterator it = seeds.begin()+random()%4;
	fa[0] = *it;
	seeds.erase(it);
	it = seeds.begin()+random()%3;
    fa[(subSize*size)] = *it;
	seeds.erase(it);
	it = seeds.begin()+random()%2;
    fa[(subSize*size)+subSize] = *it;
	seeds.erase(it);
    fa[subSize] = seeds[0];
    
    int	i, j, oddline;
    while(stride)
	{
		for(i = stride; i < subSize; i += stride)
		{
			for(j = stride; j < subSize; j += stride)
			{
				fa[(i * size) + j] = scale*fractRand(.5f)
					+ avgSquareVals(i, j, stride, size, fa);
				j += stride;
			}
			i += stride;
		}

		oddline = 0;
		for(i = 0; i < subSize; i += stride)
		{
		    oddline = (oddline == 0);
			for(j = 0; j < subSize; j += stride)
			{
				if(oddline && !j)
					j += stride;

				fa[(i * size) + j] = scale*fractRand(.5f)
					+ avgDiamondVals(i, j, stride, size, subSize, fa);

				if(!i)
					fa[(subSize*size) + j] = fa[(i * size) + j];
				if(!j)
					fa[(i*size) + subSize] = fa[(i * size) + j];
				j += stride;
			}
		}

		scale *= ratio;
		stride >>= 1;
    }
}

///////////////////////////////////////////////////////////////////

/* Once the random absract fractal (a table of floats!) is created using the
 * above code, we paint it with map tiles, using the following threshold
 * values (outdoor and dungeon separately): */

const char NUM_OD_TILES = 13;
const Tile outdoor_tiles[NUM_OD_TILES] = { T_WALL, T_WATER, T_GROUND, T_WATER,
	T_SWAMP, T_TREE, T_GROUND, T_TREE, T_ROAD, T_GROUND, T_FLOOR, T_ROUGH,
	T_WALL };
const float od_thresholds[NUM_OD_TILES] = {
	/*1.0...*/0.72f, // wall
	/*...*/0.64f, // water
	/*...*/0.5f, // ground (grass)
	/*...*/0.42f, // water
	/*...*/0.31f, // swamp
	/*...*/0.25f, // tree
	/*...*/0.1f, // ground
	/*...*/0.02f, // tree
	/*...*/-0.1f, // road
	/*...*/-0.28f, // ground
	/*...*/-0.36f, // floor (rock)
	/*...*/-0.50f, // rough
	/*...-1.0*/-2.0f // wall
};

const char NUM_UG_TILES = 11;
const Tile underground_tiles[NUM_UG_TILES] = { T_WALL, T_WATER, T_WALL, T_FLOOR,
	T_WATER, T_FLOOR, T_WALL, T_FLOOR, T_CHASM, T_FLOOR, T_WALL };
const float ug_thresholds[NUM_UG_TILES] = {
	/*1.0...*/0.6f, // wall
	/*...*/0.49f, // water
	/*...*/0.2f, // wall
	/*...*/0.3f, // floor (can be switched to rough)
	/*...*/0.2f, // water
	/*...*/0.12f, // floor (can be switched to rough)
	/*...*/-0.3f, // wall
	/*...*/-0.37f, //floor (can be switched to rough)
	/*...*/-0.5f, // chasm
	/*...*/-0.58f, //floor (can be switched to rough)
	/*...-1.0*/-2.0f // wall
};

///////////////////////////////////////////////////////////////////

/*
 * The house building routines follow. These look horrible and
 * are probably too poorly commented. But it works! Somewhat.
 */
const char MINSIZE = 4; // Minimum size of a house; DO NOT CHANGE!
const char MAX_HOUSE_SIZE = 15; // Max size; should be at least MINSIZE+2
const char CHANCE_NO_WALLS = 15; // % of having no interior walls whatsoever
const char CHANCE_WINDOW = 20;
const char CHANCE_DOOR = 35;

// buffer to hold a house while it's being generated
char house[MAX_HOUSE_SIZE*MAX_HOUSE_SIZE];
/* Symbols used:
 * '#*: outer wall
 * 'H': inner wall
 * '+': a door
 * '_': a tile not covered by the house
 * 'A': floor, "an inner wall could start here"
 * 'B': floor, "an inner wall could end here"
 * '.': fixed to be floor
 * ',': interior, not yet fixed to be floor
 */

bool wall_space_check(e_Dir backdir, const Coords c)
{
	// Check first left:
	--(--backdir);
	Coords d = (c.in(backdir)).in(backdir);
	char ch = house[d.y*MAX_HOUSE_SIZE+d.x];
	if(ch != '#' && ch != 'H' && ch != '+')
		return true; // all good
	// Else check right:
	backdir = !backdir;
	d = (c.in(backdir)).in(backdir);
	ch = house[d.y*MAX_HOUSE_SIZE+d.x];
	if(ch != '#' && ch != 'H' && ch != '+')
		return true; // all good
	// failed this check:
	return false;
}

struct WIP // wall init point
{
	Coords co;
	e_Dir fd;

	WIP(const Coords &c, const e_Dir d) : co(c), fd(d) {}
};

// returns D_MAX for "no"
e_Dir is_valid_wall_init_pt(const Coords c)
{
	char ch = house[c.y*MAX_HOUSE_SIZE+c.x];
	// Must be uninitialized interior or previously accepted as an A-point
	if(ch != ',' && ch != 'A')
		return MAX_D;
	// Must have no door in a cardinal direction and 2-3 wall neighbours
	char walls = 0;
	e_Dir dir;
	e_Dir backdir = MAX_D;
	Coords d;
	for(dir = D_N; dir != MAX_D; dir = e_Dir(dir+1))
	{
		d = c.in(dir);
		ch = house[d.y*MAX_HOUSE_SIZE+d.x];
		if(!(int(dir)%2) && ch == '+') // door in cardinal dir!
			return MAX_D;
		if(ch == '#' || ch == 'H' || ch == '+') // a door in non-card dir counts as a wall
		{
			++walls;
			if(!(int(dir)%2)) // found a wall in a cardinal direction
			{
				if(backdir != MAX_D) // found _two_ walls in a cardinal direction!
					return MAX_D;
				// else:
				backdir = dir;
			}
		}
	}
	if(walls > 3 || walls < 2)
		return MAX_D;
	// Finally, the most complicated check: in at least one side-direction there
	// must be at least two free spaces (non-walls).
	if(wall_space_check(backdir, c))
		return backdir;
	// else
	return MAX_D;
}

bool is_valid_wall_cont_pt(const Coords c)
{
	char ch = house[c.y*MAX_HOUSE_SIZE+c.x];
	// Must be uninitialized interior or previously accepted as an B-point
	if(ch != ',' && ch != 'B')
		return false;
	// Must have no more than 2 wall neighbours, and no door in cardinal dir.
	e_Dir dir;
	e_Dir backdir = MAX_D;
	char walls = 0;
	Coords d;
	for(dir = D_N; dir != MAX_D; dir = e_Dir(dir+1))
	{
		d = c.in(dir);
		ch = house[d.y*MAX_HOUSE_SIZE+d.x];
		if(!(int(dir)%2) && ch == '+') // door in cardinal dir!
			return false;
		if(ch == '#' || ch == 'H' || ch == '+')
		{
			if(++walls > 2)
				return false;
			if(!(int(dir)%2)) // found a wall in a cardinal direction
			{
				if(backdir != MAX_D) // found _two_ walls in a cardinal direction!
					return false;
				// else:
				backdir = dir;
			}
		}
	}
	if(backdir != MAX_D)
		return wall_space_check(backdir, c);
	return true; // no adjacent walls
}

bool outside(const Coords &c)
{
	return (c.x < 0 || c.y < 0 || c.x >= MAX_HOUSE_SIZE || c.y >= MAX_HOUSE_SIZE);
}

// Returns direction of window or MAX_D for "no"
e_Dir is_valid_win_pt(const Coords c)
{
	// Assuming that the tile at c is '#'.
	e_Dir dir = D_N;
	Coords d;
	for(; dir != MAX_D; dir = e_Dir(dir+2))
	{
		d = c.in(dir);
		if(outside(d))
			break;
		if(house[d.y*MAX_HOUSE_SIZE+d.x] == '_')
			break;
	}
	if(dir == MAX_D) // no outside tile in cardinal dir
		return MAX_D;
	// The tile opposite to dir has to be floor:
	d = c.in(!dir);
	if(outside(d))
		return MAX_D;
	char ch = house[d.y*MAX_HOUSE_SIZE+d.x];
	if(ch == '#' || ch == 'H' || ch == '+')
		return MAX_D;
	// Finally, the sides have to be outside walls:
	++(++dir);
	d = c.in(dir);
	if(outside(d) || house[d.y*MAX_HOUSE_SIZE+d.x] != '#')
		return MAX_D;
	d = c.in(!dir);
	if(outside(d) || house[d.y*MAX_HOUSE_SIZE+d.x] != '#')
		return MAX_D;
	return dir;
}

bool is_valid_door_pt(const Coords c)
{
	// A door can be placed where a window can.
	e_Dir dir = is_valid_win_pt(c);
	if(dir == MAX_D)
		return false;
	// However, we don't want two doors too close to each other.
	Coords d = (c.in(dir)).in(dir);
	if(!outside(d) && house[d.y*MAX_HOUSE_SIZE+d.x] == '+')
		return false;
	d = (c.in(!dir)).in(!dir);
	return (outside(d) || house[d.y*MAX_HOUSE_SIZE+d.x] != '+');
}

// Here's the entry point function.
void gen_house(const bool outdoor)
{
	// Determine actual size:
	char xsize = random()%(MAX_HOUSE_SIZE - MINSIZE + 1) + MINSIZE;
	char ysize = random()%(MAX_HOUSE_SIZE - MINSIZE + 1) + MINSIZE;
	
	// Next, we have a bunch of coordinates, some of which will be obsolete
	// for smaller houses:
	Coords tl, tr, bl, br; // corners, "top-left" through "bottom-right"
	Coords lct, lcb; // "left cut top/bottom"
	Coords rct, rcb; // "right cut top/bottom"
	/* tl                   tr
	 *  x-------------------x
	 *  |    lct            |
	 *  |---x               |
	 *  |   |         rct   |
	 *  |   |          x----|
	 *  |---x          |    |
	 *  |   lcb        x----|
	 *  |            rcb    |
	 *  x-------------------x
	 *  bl                  br
	 *
	 * It might be that there are no cuts at all, in which case lct=lcb, rct=rcb.
	 * In the end, we might rotate the whole house 90 degrees, moving the possible
	 * cuts to top&bottom instead.
	 */
	tl.x = tl.y = tr.y = bl.x = 0;
	bl.y = br.y = ysize-1;
	tr.x = br.x = xsize-1;
	lct = lcb = tl;
	rct = rcb = tr;
	
	// If possible, initialize the cuts to non-degenerate values:
	if(ysize >= MINSIZE+2 && xsize >= MINSIZE+2) // possible to have cuts
	{
		if(random()%2) // should make left cut
		{
			if(ysize >= 2*(MINSIZE+1)) // have room to move
				lct.y += MINSIZE-1 + random()%(ysize - 2*MINSIZE);
			lcb.y = lct.y + 2;
			if(ysize >= 3+MINSIZE+lcb.y)
				lcb.y += random()%(ysize - lcb.y - MINSIZE - 1);

			if(xsize > MINSIZE+2)
				lct.x = lcb.x = 2 + random()%(xsize-MINSIZE-1);
			else
				lct.x = lcb.x = 2;
		}
		// check if should and still can make the right cut:
		if(random()%2 && xsize - MINSIZE - lct.x >= 3)
		{
			if(ysize >= 2*(MINSIZE+1)) // have room to move
				rct.y += MINSIZE-1 + random()%(ysize - 2*MINSIZE);
			rcb.y = rct.y + 2;
			if(ysize >= 3+MINSIZE+rcb.y)
				rcb.y += random()%(ysize - rcb.y - MINSIZE - 1);

			if(xsize > lct.x+MINSIZE+3)
				rct.x = rcb.x -= 2 + random()%(xsize-lct.x-MINSIZE-2);
			else
				rct.x = rcb.x -= 2;
		}
	}

	// Initialize the internal representation buffer:
	Coords c;
	for(c.y = 0; c.y < MAX_HOUSE_SIZE; ++c.y)
	{
		for(c.x = 0; c.x < MAX_HOUSE_SIZE; ++c.x)
		{
			if(c.x < xsize && c.y < ysize)
				house[c.y*MAX_HOUSE_SIZE+c.x] = '#';
			else
				house[c.y*MAX_HOUSE_SIZE+c.x] = '_';
		}
	}
	// Next, add the holes caused by the cuts:
	if(!lct.y && lcb.y)
	{
		for(c.x = 0; c.x < lct.x; c.x++)
		{
			for(c.y = 0; c.y < lcb.y; c.y++)
				house[c.y*MAX_HOUSE_SIZE+c.x] = '_';
		}
	}
	else if(lct.y)
	{
		for(c.x = 0; c.x < lct.x; c.x++)
		{
			for(c.y = lct.y+1; c.y < lcb.y; c.y++)
				house[c.y*MAX_HOUSE_SIZE+c.x] = '_';
		}
	}
	if(!rct.y && rcb.y)
	{
		for(c.x = rct.x+1; c.x < xsize; c.x++)
		{
			for(c.y = 0; c.y < rcb.y; c.y++)
				house[c.y*MAX_HOUSE_SIZE+c.x] = '_';
		}
	}
	else if(rct.y)
	{
		for(c.x = rct.x+1; c.x < xsize; c.x++)
		{
			for(c.y = rct.y+1; c.y < rcb.y; c.y++)
				house[c.y*MAX_HOUSE_SIZE+c.x] = '_';
		}
	}
	// Finally, "carve the whole house hollow":
	bool inside; Coords d;
	for(c.y = 1; c.y < ysize-1; ++c.y)
	{
		for(c.x = 1; c.x < xsize-1; ++c.x)
		{
			inside = true;
			for(d.x = c.x-1; d.x <= c.x+1; d.x++)
			{
				for(d.y = c.y-1; d.y <= c.y+1; d.y++)
				{
					if(house[d.y*MAX_HOUSE_SIZE+d.x] == '_')
					{
						inside = false;
						goto exitall;
					}
				}
			}
			exitall:
			if(inside)
				house[c.y*MAX_HOUSE_SIZE+c.x] = ',';
		}
	}

	// Next we decide the location of the main entrance:
	Coords door1(xsize/2, ysize/2);
	e_Dir dir;
	if(house[door1.y*MAX_HOUSE_SIZE+door1.x] == '_') // this could be bad
	{
		door1.x = door1.y = 0;
		dir = D_SE; // this assures we'll hit something
	}
	else // this is what we do practically always
		dir = e_Dir(random()%MAX_D);
	while(house[door1.y*MAX_HOUSE_SIZE+door1.x] != '#')
		door1 = door1.in(dir);
	house[door1.y*MAX_HOUSE_SIZE+door1.x] = '+';

	// Build walls inside the house:
	if(xsize > MINSIZE || ysize > MINSIZE)
	{
		Coords c2;
		// We put _something_ inside the house, depending on its size, at least.
		// Tiles marked as 'H' (wall) or '.' (final floor) won't be touched
		// by subsequent procedures.
		// The first possibility is a "pillar hall":
		if(random()%2)
		{
			short area_tl = lct.y*rct.x;
			short area_tr = rct.y*(tr.x - lct.x);
			short area_bl = (bl.y - lcb.y)*rcb.x;
			short area_br = (br.y - rcb.y)*(tr.x - lct.x);
			if(area_tl > area_tr && area_tl > area_bl && area_tl > area_br)
			{
				c = tl;
				c2.x = rct.x; c2.y = lct.y;
			}
			else if(area_tr > area_tl && area_tr > area_bl && area_tr > area_br)
			{
				c.x = lct.x; c.y = 0;
				c2.x = tr.x; c2.y = rct.y;
			}
			else if(area_bl > area_tl && area_bl > area_tr && area_bl > area_br)
			{
				c.x = 0; c.y = lcb.y;
				c2.x = rcb.x; c2.y = bl.y;
			}
			else
			{
				c.x = lcb.x; c.y = rcb.y;
				c2 = br;
			}

			c.x++; c.y++;
			c2.x--; c2.y--;
			// Now the area between c and c2 is the largest rectangle in the house.

			if((c2.x - c.x) >= 4 && !((c2.x - c.x)%2)
				&& (c2.y - c.y) >= 4 && !((c2.y - c.y)%2))
			{
				for(d.x = c.x; d.x <= c2.x; d.x++)
				{
					for(d.y = c.y; d.y <= c2.y; d.y++)
					{
						if((d.y - c.y)%2 && (d.x - c.x)%2)
							house[d.y*MAX_HOUSE_SIZE+d.x] = 'H';
						else
							house[d.y*MAX_HOUSE_SIZE+d.x] = '.';
					}
				}
			}
		} // if try to do pillar hall

		// Next we add some smaller rooms/corridors/whatnot.
		if(random()%100 >= CHANCE_NO_WALLS)
		{
		char ch; char walls;
		vector<WIP> As;
		vector<WIP>::iterator Ait;
		e_Dir firstdir;
		char wall_len;
		bool canturn;
		// Build 1-4 walls (or stop when cannot build any more)
		for(walls = 1+random()%4; walls > 0; --walls)
		{
			As.clear();
			// Mark by 'A' the tiles where a wall can "start", and by 'B' the tiles
			// where a wall can exists otherwise:
			for(c.y = 1; c.y < ysize-1; ++c.y)
			{
			for(c.x = 1; c.x < xsize-1; ++c.x)
			{
				if((dir = is_valid_wall_init_pt(c)) != MAX_D)
				{
					house[c.y*MAX_HOUSE_SIZE+c.x] = 'A';
					As.push_back(WIP(c, !dir));
				}
				else if(is_valid_wall_cont_pt(c))
					house[c.y*MAX_HOUSE_SIZE+c.x] = 'B';
				else
				{
					ch = house[c.y*MAX_HOUSE_SIZE+c.x];
					if(ch == 'A' || ch == 'B') // must reset
						house[c.y*MAX_HOUSE_SIZE+c.x] = '.';
				}
			} }
			if(As.empty())
				break;
			// Pick a random A-point to begin from:
			Ait = As.begin() + randor0(As.size());
			c = Ait->co; // store in c and remove from the vector:
			firstdir = Ait->fd;
			As.erase(Ait);
			house[c.y*MAX_HOUSE_SIZE+c.x] = 'H';
			wall_len = 0;
			dir = firstdir;
			canturn = true;
			for(;;)
			{
				d = c.in(dir);
				if(house[d.y*MAX_HOUSE_SIZE+d.x] == 'B')
					house[d.y*MAX_HOUSE_SIZE+d.x] = 'H';
				else if(canturn)
				{
					canturn = false;
					++(++dir);
					if(random()%2) dir = !dir;
					d = c.in(dir);
					if(house[d.y*MAX_HOUSE_SIZE+d.x] == 'B')
						house[d.y*MAX_HOUSE_SIZE+d.x] = 'H';
					else
					{
						d = c.in(!dir);
						if(house[d.y*MAX_HOUSE_SIZE+d.x] == 'B')
							house[d.y*MAX_HOUSE_SIZE+d.x] = 'H';
						else
							break;
					}
				}
				else // no more suitable B-points found
					break;
				c = d; // continue from the neighbour point
				++wall_len;
			}
			// Now there is no more a 'B' neighbour; check if there is an 'A' neighbour:
			dir = e_Dir(2*(random()%4)); // gives a random cardinal dir
			for(ch = 0; ch < 4; ++ch)
			{
				d = c.in(dir);
				// If wall_len is 0 (no B points used), only allow using an
				// A-point in firstdir. Never allow it in !firstdir.
				if((wall_len || dir == firstdir)
					&& dir != !firstdir
					&& house[d.y*MAX_HOUSE_SIZE+d.x] == 'A')
				{
					// make sure there is a door:
					if(random()%2)
					{
						house[d.y*MAX_HOUSE_SIZE+d.x] = 'H';
						house[c.y*MAX_HOUSE_SIZE+c.x] = '+';
					}
					else
					{
						house[c.y*MAX_HOUSE_SIZE+c.x] = '+';
						house[d.y*MAX_HOUSE_SIZE+d.x] = 'H';
					}
					break;
				}
				++(++dir);
			}
		} // 1-4 walls
		} // should add rooms
	} // can add rooms

	// Center the house inside h; move each point n steps right and m steps down,
	// filling with n and m '_':s respectively:
	char n = (MAX_HOUSE_SIZE-xsize)/2;
	char m = (MAX_HOUSE_SIZE-ysize)/2;
	if(n || m)
	{
		for(c.y = ysize-1+m; c.y >= m; --c.y)
		{
			for(c.x = xsize-1+n; c.x >= n; --c.x)
			{
				house[c.y*MAX_HOUSE_SIZE+c.x] = house[(c.y-m)*MAX_HOUSE_SIZE+c.x-n];
				house[(c.y-m)*MAX_HOUSE_SIZE+c.x-n] = '_';
			}
		}
	}

	// Possible rotate the house 90 degrees (a square matrix transponation)
	if(random()%2)
	{
		char tmp;
		for(n = 0; n < MAX_HOUSE_SIZE-1; ++n)
		{
	    	for(m = n + 1; m < MAX_HOUSE_SIZE; ++m)
			{
				tmp = house[n*MAX_HOUSE_SIZE+m];
				house[n*MAX_HOUSE_SIZE+m] = house[m*MAX_HOUSE_SIZE+n];
				house[m*MAX_HOUSE_SIZE+n] = tmp;
			}
		}
	}

	// Add doors & windows. Has to be done after possible rotate to
	// align windows properly.
	n = 0; // number of doors; at most 2 addition ones
	for(c.y = 0; c.y < ysize; ++c.y)
	{
		for(c.x = 0; c.x < xsize; ++c.x)
		{
			if(house[c.y*MAX_HOUSE_SIZE+c.x] == '#') // outer wall
			{
				if(n < 2 && random()%100 < CHANCE_DOOR
					&& is_valid_door_pt(c))
				{
					house[c.y*MAX_HOUSE_SIZE+c.x] = '+';
					++n;
				}
				else if(outdoor && random()%100 < CHANCE_WINDOW
					&& (dir = is_valid_win_pt(c)) != MAX_D)
				{
					if((int(dir)%4))
						house[c.y*MAX_HOUSE_SIZE+c.x] = '-';
					else
						house[c.y*MAX_HOUSE_SIZE+c.x] = '|';
				}
			}
		}
	} // add doors/windows loop
} // gen_house

///////////////////////////////////////////////////////////////////

/* We'll typically generate a number of houses, and place them using
 * one of the following algorithms. */

// 'circular' places the house in a circle
void fill_hc_circular(vector<Coords> &hc, const char num, const short size)
{
	// putting just one point onto a circle is silly:
	if(num == 1)
	{
		hc[0].x = size/3 + random()%(size/3);
		hc[0].y = size/3 + random()%(size/3);
		return;
	}
	// The angle between the points, 2Pi/num:
	float theta = 6.28318f/num;
	// We want the points to be at a *walking* distance of MAX_HOUSE_SIZE+1:
	short rad = (MAX_HOUSE_SIZE+1)/max(abs(1.0f-cos(theta)), abs(sin(theta)))+1;
	/* NOTE: the maximum number of houses being limited by map size should
	 * ensure that rad cannot be too large. If rad is too large the houses
	 * will be put outside of the map... */
	if(rad >= size/2 - MAX_HOUSE_SIZE)
	{
#ifdef DEBUG
		cerr << "Debug warning: circular house placement has too big radius!" << endl;
#endif
		rad = size/2 - MAX_HOUSE_SIZE - 1;
	}
	for(char i = 0; i < num; ++i)
	{
		hc[i].x = size/2 + rad*cos(theta*i);
		hc[i].y = size/2 + rad*sin(theta*i);
	}
}

// 'old' because this was the only method used in version 1. It's been modified
// since.
void fill_hc_old(vector<Coords> &hc, const char num, const short size)
{
	hc[0].x = size/3 + random()%(size/3);
	hc[0].y = size/3 + random()%(size/3);
	for(char i = 1; i < num; ++i)
	{
		hc[i].x = hc[i-1].x + MAX_HOUSE_SIZE + 1 + random()%MAX_HOUSE_SIZE;
		hc[i].y = hc[i-1].y + random()%7 - 3;
		if(hc[i].x >= size - MAX_HOUSE_SIZE - 1) // wen't too far
		{
			hc[i].x = 2 + random()%10;
			hc[i].y += MAX_HOUSE_SIZE + 1 + random()%MAX_HOUSE_SIZE;
			if(hc[i].y >= size - MAX_HOUSE_SIZE - 1)
				hc[i].y = 2 + random()%10;
		}
	}
}

// 'mainst' makes a "main street"; two lines of houses
void fill_hc_mainst(vector<Coords> &hc, const char num, const short size)
{
	Coords c1, c2;
	// random edge points, not too close to corners
	if(random()%2)
	{
		c1.x = (random()%2)*(size-1);
		c1.y = random()%(size/3)+size/3;
	}
	else
	{
		c1.y = (random()%2)*(size-1);
		c1.x = random()%(size/3)+size/3;
	}
	// Place c2 on another edge:
	if(!(c1.x % (size-1))) // opposite x-wise
	{
		c2.x = (c1.x + size-1)%(2*(size-1));
		c2.y = random()%(size/3)+size/3;
	}
	else // opposite y-wise
	{
		c2.y = (c1.y + size-1)%(2*(size-1));
		c2.x = random()%(size/3)+size/3;
	}
	// If necessary, switch so that c1 is closer to (0,0):
	if(c1.dist_walk(Coords(0,0)) > c2.dist_walk(Coords(0,0)))
	{
		Coords tmp = c1;
		c1 = c2;
		c2 = tmp;
	}
	// The directional vector of the "street":
	Coords v(c2.x - c1.x, c2.y - c1.y);
	// The normal of the directional vector, shrunk:
	Coords p(-v.y, v.x);
	float len = p.dist_eucl(Coords(0,0));
	p.x = p.x*2*MAX_HOUSE_SIZE/(3*len);
	p.y = p.y*2*MAX_HOUSE_SIZE/(3*len);
	// Determine step (how far "down the street" the next houses are):
	v.x /= num+1;
	if(v.x > 3*MAX_HOUSE_SIZE/2)
		v.x = 3*MAX_HOUSE_SIZE/2;
	v.y /= num+1;
	if(v.y > 3*MAX_HOUSE_SIZE/2)
		v.y = 3*MAX_HOUSE_SIZE/2;
	// Place the coords:
	for(char i = 0; i < num; i += 2)
	{
		hc[i].x = c1.x + (i+1)*v.x + p.x;
		hc[i].y = c1.y + (i+1)*v.y + p.y;
		if(i+1 < num)
		{
			hc[i+1].x = c1.x + (i+1)*v.x - p.x;
			hc[i+1].y = c1.y + (i+1)*v.y - p.y;
		}
	}
}

// Use function pointers to easily pick a random algorithm:
const char NUM_FHC_FUNCTIONS = 3;
void (*fill_hcoord_func[NUM_FHC_FUNCTIONS])(vector<Coords> &hc, const char num,
	const short size) = { fill_hc_circular, fill_hc_old, fill_hc_mainst };

} // end local namespace

#ifndef MAPTEST
// also needed in minimap generation + needs to be outside of local namespace
bool operator<(const Tile &lhs, const Tile &rhs)
{
	return lhs.symbol < rhs.symbol;
}
#endif


// Here the whole map is generated.
Map::Map(const short size, const short variation, const short players)
{
	// randomise map size
	mapsize = size;
	unsigned short sh = int(size)*variation/100;
	if(sh)
		mapsize += (random() % (2*sh)) - sh;
	
	// make sure there's enough room
	sh = 10*sqrt(players); /* With the maximum amount of players (200), this
		gives 141. */
	if(mapsize < sh)
	{
		mapsize = sh;
#ifndef MAPTEST
		timed_log("Note: getting crowded, mapsize adjusted to "
			+ boost::lexical_cast<string>(mapsize));
#endif
	}

	// Allocate space for the map:
	make_right_size(data);
	/* A theoretical point: does this not conflict itself somehow? The method
	 * make_right_size() is const, so it "does not change the content of the
	 * object", but it can change its argument, and we are passing as an
	 * argument a reference to a part of the object...
	 * Oh well. As long as it compiles! */
	
	// Now we start generating the map content. The first step is to generate
	// the background using something like 2D-fractals. The algorith requires
	// a square with side length a power of two. Figure out smallest possible:
	sh = 64;
	while(sh < mapsize)
		sh <<= 1;
	// mapsize cannot be larger than 511 (settings.cpp: 365 + 40%), so sh
	// is at most 512.
	++sh;
	float* mesh = new float[sh*sh]; // required by the fractal algorithm
	float h; // the coarseness factor for the map generation
	const float *thresholds;
	const Tile *tiles;
	char num_tile_types;
	// Determine map type:
	if((maptype = Config::next_map_type()) == MT_OUTDOOR)
	{
		// best values for h are in the range 0.28--0.34
		h = 0.31f + fractRand(0.03f);
		thresholds = od_thresholds;
		tiles = outdoor_tiles;
		num_tile_types = NUM_OD_TILES;
	}
	else // dungeon, best values for h seem to be 0.4--0.5
	{
		h = 0.45f + fractRand(0.05f);
		thresholds = ug_thresholds;
		tiles = underground_tiles;
		num_tile_types = NUM_UG_TILES;
	}
	fill2DFractArray(mesh, sh, h);
	// The "fractal" was generated; convert into Tile-representation.
	// Note that we ignore a lot of the generated data here, unless
	// mapsize is a power of 2.
    short i, j;
	char k;
	Tile *tp;
    for(i = 0; i < mapsize; ++i)
	{
		for(j = 0; j < mapsize; ++j)
		{
			tp = mod_tile(i, j);
			for(k = 0; k < num_tile_types; ++k)
			{
				// The last test (when k == num_tile_types-1) always
				// succeeds, since the mesh values are -1...1!
				if(mesh[(j*sh)+i] > thresholds[k])
				{
					*tp = tiles[k];
					break;
				}
			}
		}
    }
	delete[] mesh;

	// If we are creating an underground map, we add corridors and randomly
	// change the floor tiles to rough tiles:
	char ch;
	if(maptype != MT_OUTDOOR)
	{
		// Corridor algorithm; kind of like maze generation:
		ch = random()%8 + 8; // 8--15
		sh = mapsize/ch;
		Coords cell(random()%sh, random()%sh), cell2;
		vector<Coords> mazed(1, cell);
		bool widen;
		e_Dir d;
		for(;;)
		{
			// set cell2 to a rand neighbour of 'cell' which is not in 'mazed':
			d = e_Dir(random()%MAX_D);
			for(i = 0; i < MAX_D; ++i)
			{
				cell2 = cell.in(d);
				if(cell2.x >= 0 && cell2.y >= 0 && cell2.x < sh && cell2.y < sh
					&& find(mazed.begin(), mazed.end(), cell2) == mazed.end())
					break; // this will do
				++d;
			}
			if(i == MAX_D) // no suitable neighbours available!
			{
				// This means 'cell' is an end of a corridor; make a patch
				// of floor there, possibly:
				if(random()%2)
					add_patch(Coords(cell.x*ch+ch/2, cell.y*ch+ch/2), T_FLOOR);
				// see if have generated enough (5/8 a mapfull):
				if(mazed.size() < 5u*sh*sh/8u) // no
				{
					// try again, with a different 'cell':
					cell2 = cell; // store old value of 'cell'
					do cell = mazed[random()%mazed.size()];
					while(cell == cell2);
					continue;
				}
				else // yes
					break;
			} // else:
			mazed.push_back(cell2);
			// make a corridor between 'cell' and 'cell2'
			widen = random()%3; // every 3rd corridor is wide
			cell.x = cell.x*ch + ch/2; // convert cells to middle pt coords
			cell.y = cell.y*ch + ch/2;
			cell2.x = cell2.x*ch + ch/2;
			cell2.y = cell2.y*ch + ch/2;
			while(!(cell == cell2))
			{
				data[cell.y][cell.x] = T_FLOOR;
				if(widen)
					data[cell.y+1][cell.x] = T_FLOOR;
				cell = cell.in(d);
			}
			// prepare for next run:
			cell = mazed[random()%mazed.size()];
		}

		// Change of floor->rough:
		for(i = 0; i < mapsize; ++i)
		{
			for(j = 0; j < mapsize; ++j)
			{
				if(random()%100 < CHANCE_ROUGH && data[j][i] == T_FLOOR)
					data[j][i] = T_ROUGH;
			}
		}
	}

	// Next, we create some houses. The map is inhabited if the number of houses
	// is high enough.
	char num_houses = random()%(19*(mapsize+42)/469); // rand()%(3...22)
	inhabited = (num_houses >= 19*(mapsize+42)/938);
	if(num_houses)
	{
		vector<Coords> hcoords(num_houses);
		fill_hcoord_func[rand()%NUM_FHC_FUNCTIONS](hcoords, num_houses, mapsize);
		vector<Coords>::const_iterator ci;
		for(; num_houses > 0; --num_houses)
		{
			ci = hcoords.begin();
			/*if(ci->x < 0 || ci->y < 0)
				continue;*/
			gen_house(maptype == MT_OUTDOOR);
			// That generated a "house pattern" to the table 'house'. Now we
			// must apply this pattern to the location 'ci' on the actual map (with
			// 'ci' giving the NW corner of, not the house, but the house buffer).
			// First loop blows up areas around doors&windows:
			for(i = 0; i < MAX_HOUSE_SIZE; ++i)
			{
				for(j = 0; j < MAX_HOUSE_SIZE; ++j)
				{
					ch = house[j*MAX_HOUSE_SIZE+i];
					if(ch == '+')
					{
						if(maptype == MT_OUTDOOR)
							add_patch(Coords(ci->x+i, ci->y+j), T_ROAD);
						else
							add_patch(Coords(ci->x+i, ci->y+j), T_FLOOR);
					}
					else if(ch == '|' || ch == '-')
						add_patch(Coords(ci->x+i, ci->y+j), T_GROUND);
				}
			}
			// Second run actually puts the house there:
			for(i = 0; i < MAX_HOUSE_SIZE; ++i)
			{
				if(ci->x+i >= mapsize)
					break;
				for(j = 0; j < MAX_HOUSE_SIZE; ++j)
				{
					if(ci->y+j >= mapsize)
						break;
					tp = mod_tile(ci->x + i, ci->y + j);
					switch(house[j*MAX_HOUSE_SIZE+i])
					{
					case 'A': case 'B': case ',': case '.':
						*tp = T_FLOOR; break;
					case 'H': case '#':
						*tp = T_WALL; break;
					case '+':
						*tp = T_DOOR;
						if(random()%2) // open instead of closed
						{
							tp->symbol = '\\';
							tp->flags |= TF_WALKTHRU|TF_SEETHRU;
						}
						break;
					case '|': case '-':
						*tp = T_WINDOW;
						tp->symbol = house[j*MAX_HOUSE_SIZE+i];
						break;
					// default: do nothing
					}
				}
			}

			hcoords.erase(hcoords.begin());
		} // loop creating houses
	} // if create houses

	// Always finish with the boundary wall, undiggable:
	rowit rit = data.begin();
	vector<Tile>::iterator it;
	for(it = rit->begin(); it != rit->end(); ++it)
	{
		*it = T_WALL;
		it->flags |= TF_NODIG;
	}
	rit = data.begin()+mapsize-1;
	for(it = rit->begin(); it != rit->end(); ++it)
	{
		*it = T_WALL;
		it->flags |= TF_NODIG;
	}
	for(rit = data.begin(); rit != data.end(); ++rit)
	{
		(*rit)[0] = (*rit)[mapsize-1] = T_WALL;
		(*rit)[0].flags |= TF_NODIG;
		(*rit)[mapsize-1].flags |= TF_NODIG;
	}
	

	// Finally, initialize the BY_WALL flags (this might be doable faster, too)
	// and make some walls undiggable. Note that no need to init for the edge
	// since it's always undiggable.
	Coords c;
	c.y = 1;
	while(c.y < mapsize-1)
	{
		c.x = 1;
		while(c.x < mapsize-1)
		{
			upd_by_wall_flag(c);
			if(!(random()%EVERY_N_WALL_UNDIG[maptype]) && data[c.y][c.x] == T_WALL)
				data[c.y][c.x].flags |= TF_NODIG;
			c.x++;
		}
		c.y++;
	}

	// print out the map (or at least a part of it) to stderr.
#ifdef MAPTEST
	for(rowit it = data.begin(); it != data.end(); ++it)
	{
		for(vector<Tile>::iterator i = it->begin(); i != it->end(); ++i)
			cerr << i->symbol;
		cerr << endl;
	}
	cerr << "h=" << h;
	if(inhabited) cerr << "; inhabited";
	cerr << endl;
#endif
}

Map::~Map() {}


void Map::upd_by_wall_flag(const Coords &c)
{
	Coords cn;
	e_Dir d = D_N;
	for(;;)
	{
		cn = c.in(d);
		if(data[cn.y][cn.x] == T_WALL)
		{
			data[c.y][c.x].flags |= TF_BYWALL;
			return;
		}
		if(++d == D_N) // went around!
		{
			data[c.y][c.x].flags &= ~(TF_BYWALL);
			break;
		}
	}
}


#ifndef MAPTEST
Coords Map::get_center_of_sector(const e_Dir d) const
{
	Coords c(mapsize/2, mapsize/2);
	if(d == MAX_D)
		return c;
	Coords shift(0,0);
	shift = shift.in(d);
	shift.x *= mapsize/3;
	shift.y *= mapsize/3;
	c.x += shift.x;
	c.y += shift.y;
	return c;
}
#endif


#if 0
void Map::get_copy(vector< vector<Tile> > &res) const
{
	res = data; // simple, but this does a lot of copying...
}
#endif
void Map::make_right_size(std::vector< std::vector<Tile> > &res) const
{
	// Resize the "inner vectors" (rows) that will remain
	for(short i = min(short(res.size()), mapsize)-1; i >= 0; --i)
		res[i].resize(mapsize, EMPTY_TILE);
	// Resize the outer vector (the collection of rows):
	res.resize(mapsize, vector<Tile>(mapsize, EMPTY_TILE));
}

bool Map::point_out_of_map(const Coords &c) const
{
	return (c.x < 0 || c.x >= mapsize
		|| c.y < 0 || c.y >= mapsize);
}


// Create a "circular" (the "wrong circular") patch with Tile t at c, rad. 2-4
void Map::add_patch(const Coords c, const Tile t)
{
	char radius = random()%3 + 2; //2..4
	short x, y, cmp;
	for(y = max(0, c.y - radius); y <= min(mapsize-1, c.y + radius); ++y)
	{
		for(x = max(0, c.x - radius); x <= min(mapsize-1, c.x + radius); ++x)
		{
			if((cmp = (c.x-x)*(c.x-x)+(c.y-y)*(c.y-y)) <= radius*radius)
				data[y][x] = t;
		}
	}
}


#ifndef MAPTEST
Tile Map::subsample_tile(const Tile &t) const
{
	// 't' is assumed to be a tile of a type of which there exists
	// only a few in a sampling area. We "change it into" something
	// more representative.
	if(t == T_TREE)
		return T_GROUND;
	if(t == T_SWAMP)
		return T_WATER;
	if(t == T_ROUGH)
		return T_FLOOR;
	if(t.symbol == '\\' || t.symbol == '+') // doors
		return T_FLOOR;
	return t;
}


void Map::gen_miniview(char *target) const
{
	short F = mapsize/VIEWSIZE;
	short x, y, ax, ay;
	map<Tile, short> amounts;
	map<Tile, short>::iterator it;
	Tile t;

	for(y = 0; y < VIEWSIZE; ++y)
	{
		for(x = 0; x < VIEWSIZE; ++x)
		{
			// collect tile amounts in this area:
			for(ay = y*F+1; ay < (y+1)*F-1; ++ay)
			{
				for(ax = x*F; ax < (x+1)*F; ++ax)
					amounts[data[ay][ax]]++;
			}
			// Do some reduction. Think of this as a "crude sinc filter"
			for(it = amounts.begin(); it != amounts.end(); ++it)
			{
				// The reference value here is the fact that there are a total
				// of F*F tiles!
				if(it->second < 7*F*F/24
					&& (t = subsample_tile(it->first)) != it->first)
				{
					amounts[t] += it->second;
					amounts.erase(it);
					it = amounts.begin(); // start over!
				}
			}
#if 0
			// To make the minimap look nicer and be more informative, we
			// "highlight" trees, swamp and lakes:
			if(amounts[T_TREE] >= amounts[T_GROUND]/4
				|| amounts[T_SWAMP] >= amounts[T_GROUND]/4
				|| amounts[T_WATER] >= amounts[T_GROUND]/4)
				amounts.erase(T_GROUND);
#endif
			// now just represent this area by the most common tile:
			t = max_element(amounts.begin(), amounts.end(), pred)->first;
			*(target++) = t.cpair;
			*(target++) = t.symbol;
			amounts.clear();
		}
	}
}


e_Dir Map::coords_in_sector(const Coords &c) const
{
	if(c.x < mapsize/3)
	{
		if(c.y < mapsize/3)
			return D_NW;
		if(c.y < 2*mapsize/3)
			return D_W;
		return D_SW;
	}
	if(c.x < 2*mapsize/3)
	{
		if(c.y < mapsize/3)
			return D_N;
		if(c.y < 2*mapsize/3)
			return MAX_D;
		return D_S;
	}
	if(c.y < mapsize/3)
		return D_NE;
	if(c.y < 2*mapsize/3)
		return D_E;
	return D_SE;
}


bool Map::LOS_between(const Coords &c1, Coords c2, const char maxrad)
{
	char r = c2.dist_walk(c1);
	if(r <= maxrad) // close enough
	{
		// Check if there is a LOS between c1 and c2:
		if(r <= 1)
			return true; // seen, done
		// Construct vector from c1 to c2 (put it in c2):
		c2.x -= c1.x;
		c2.y -= c1.y;
		char line, ind;
		if(c2.y == -r) line = c2.x + r;
		else if(c2.x == r) line = 3*r + c2.y;
		else if(c2.y == r) line = 5*r - c2.x;
		else /* c2.x == -r */ line = 7*r - c2.y;
		for(ind = 0; ind < 2*(r-1); ind += 2)
		{
			if(!(data[loslookup[r-2][line*2*r+ind+1]+c1.y]
				[loslookup[r-2][line*2*r+ind]+c1.x].flags & TF_SEETHRU))
				break;
		}
		if(ind == 2*(r-1)) // got all the way!
			return true; // seen, done
	}
	return false;
}


bool Map::load_from_file(const string &mapname)
{
	ifstream file((Config::get_mapdir() + mapname).c_str(), ios_base::binary);
	if(!file)
		return false;
	// else:
	unsigned char mapvers;	
	file.read(reinterpret_cast<char*>(&mapvers), 1);
	if(mapvers > MAP_STORE_VERSION)
		return false; // the map is of newer version; can't be certain it will work
	file.read(reinterpret_cast<char*>(&mapsize), sizeof(short));
	if(mapsize < MIN_MAP_SIZE || mapsize > MAX_MAP_SIZE)
		return false; // a corrupt file or something
	file.read(reinterpret_cast<char*>(&inhabited), sizeof(bool));
	file.read(reinterpret_cast<char*>(&maptype), sizeof(e_MapType));
	make_right_size(data);
	vector<Tile>::iterator i;	
	for(rowit it = data.begin(); it != data.end(); ++it)
	{
		for(i = it->begin(); i != it->end(); ++i)
		{
			file.read(reinterpret_cast<char*>(&(*i)), sizeof(Tile));
			// ignore the "non-static" flags (these get saved along with the rest!)
			i->flags |= STATIC_TF;
			// validate other data:
			if(!isprint(i->symbol) || i->cpair > C_WATER || i->cpair < C_TREE)
				return false; // this is bogus data!
		}
		if(!file)
			return false; // file corrupt or something else wrong
	}
	return true;
}
#endif // not maptest build


bool Map::save_to_file(const std::string &mapname) const
{
	ofstream file((Config::get_mapdir() + mapname).c_str(), ios_base::binary);
	if(!file)
		return false;
	// else:
	file.write(reinterpret_cast<const char*>(&MAP_STORE_VERSION), 1);
	file.write(reinterpret_cast<const char*>(&mapsize), sizeof(short));
	file.write(reinterpret_cast<const char*>(&inhabited), sizeof(bool));
	file.write(reinterpret_cast<const char*>(&maptype), sizeof(e_MapType));
	vector<Tile>::const_iterator i;	
	for(vector< vector<Tile> >::const_iterator it = data.begin(); it != data.end(); ++it)
	{
		for(i = it->begin(); i != it->end(); ++i)
			file.write(reinterpret_cast<const char*>(&(*i)), sizeof(Tile));
	}
	return true;
}


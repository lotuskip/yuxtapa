// Please see LICENSE file.
#include "map.h"
#include "log.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <map>
#include <vector>
#include <boost/lexical_cast.hpp>
#ifdef DEBUG
#include <iostream>
#endif
#include "../common/constants.h"
#include "../common/util.h"
#include "../common/los_lookup.h"

namespace
{
using namespace std;

const char CHANCE_ROUGH = 22; // how much of the floor in dungeons is rough, %

const Tile T_TREE = { TF_WALKTHRU|TF_SEETHRU, 'T', C_TREE };
const Tile T_GROUND = { TF_WALKTHRU|TF_SEETHRU, '.', C_GRASS };
const Tile T_WALL = { 0, '#', C_WALL };
const Tile T_CHASM = { TF_WALKTHRU|TF_SEETHRU|TF_KILLS, ' ', C_FLOOR };
const Tile T_SWAMP = { TF_WALKTHRU|TF_SLOWWALK|TF_SEETHRU, '\"', C_TREE };
const Tile T_ROUGH = { TF_WALKTHRU|TF_SLOWWALK|TF_SEETHRU, ';', C_WALL };
const Tile T_WATER = { TF_WALKTHRU|TF_SEETHRU|TF_DROWNS, '~', C_WATER };
const Tile T_ROAD = { TF_WALKTHRU|TF_SEETHRU, '.', C_ROAD };
const Tile T_DOOR = { 0, '+', C_DOOR };
const Tile T_WINDOW = { TF_SEETHRU, '|', C_WALL };

typedef vector< vector<Tile> >::iterator rowit;
typedef vector< pair<Coords, Coords> >::const_iterator sectit;

bool operator!=(const Tile &lhs, const Tile &rhs)
{
	return lhs.symbol != rhs.symbol || lhs.cpair != rhs.cpair;
}

bool operator==(const Tile &lhs, const Tile &rhs)
{
	return !(lhs != rhs);
}

//needed in minimap generation:
bool pred(const pair<Tile, short> &lhs, const pair<Tile, short> &rhs)
{
	return lhs.second < rhs.second;
}

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
		return float(fa[(i*size) + j-stride] + fa[(i*size) + j+stride]
		+ fa[((subSize-stride)*size) + j] + fa[((i+stride)*size) + j])*.25f;
    if(i == size-1)
		return float(fa[(i*size) + j-stride] + fa[(i*size) + j+stride]
		+ fa[((i-stride)*size) + j] + fa[((0+stride)*size) + j])*.25f;
    if(!j)
		return float(fa[((i-stride)*size) + j] + fa[((i+stride)*size) + j]
		+ fa[(i*size) + j+stride] +	 fa[(i*size) + subSize-stride])*.25f;
    if(j == size-1)
		return float(fa[((i-stride)*size) + j] + fa[((i+stride)*size) + j]
		+ fa[(i*size) + j-stride] + fa[(i*size) + 0+stride])*.25f;
    return float(fa[((i-stride)*size) + j] + fa[((i+stride)*size) + j]
		+ fa[(i*size) + j-stride] + fa[(i*size) + j+stride])*.25f;
}

float avgSquareVals(const int i, const int j, const int stride, const int size,
	const float *fa)
{
    return float(fa[((i-stride)*size) + j-stride]
		+ fa[((i-stride)*size) + j+stride] + fa[((i+stride)*size) + j-stride]
		+ fa[((i+stride)*size) + j+stride])*.25f;
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
	// completely flooded or contain almost solely rock...)
	vector<float> seeds(4);
	seeds[0] = .8f + fractRand(.2f);
	seeds[1] = fractRand(.4f);
	seeds[2] = fractRand(.4f);
	seeds[3] = -.8f + fractRand(.2f);

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

const char NUM_OD_TILES = 9;
const Tile outdoor_tiles[NUM_OD_TILES] = { T_WALL, T_WATER, T_SWAMP, T_TREE,
	T_GROUND, T_ROAD, T_FLOOR, T_ROUGH, T_WALL };
const float od_thresholds[NUM_OD_TILES] = {
	/*1.0...*/0.8f, // wall
	/*...*/0.5f, // water
	/*...*/0.4f, // swamp
	/*...*/0.25f, // tree
	/*...*/-0.1f, // ground (grass)
	/*...*/-0.2f, // road
	/*...*/-0.35f, // floor (rock)
	/*...*/-0.5f, // rough
	/*...-1.0*/-2.0f // wall
};

const char NUM_UG_TILES = 6;
const Tile underground_tiles[NUM_UG_TILES] = { T_WALL, T_WATER, T_FLOOR,
	T_WALL, T_FLOOR, T_CHASM };
const float ug_thresholds[NUM_UG_TILES] = {
	/*1.0...*/0.5f, // wall
	/*...*/0.35f, // water
	/*...*/0.1f, // floor (can be switched to rough)
	/*...*/ -0.1f, // wall
	/*...*/ -0.5f, //floor (can be switched to rough)
	/*...-1.0*/-2.0f // chasm
};

///////////////////////////////////////////////////////////////////

/*
 * The house building routines follow. These look horrible and
 * are probably too poorly commented. But it works! Somewhat.
 */
const char MINSIZE = 4; // Minimum size of a house; DO NOT CHANGE
const char MAX_HOUSE_SIZE = 15; // Max size; should be at least MINSIZE+2
const char CHANCE_NO_WALLS = 15; // % of having no interior walls whatsoever
const char CHANCE_WINDOW = 20;
const char CHANCE_DOOR = 35;

char house[MAX_HOUSE_SIZE*MAX_HOUSE_SIZE];

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


} // end local namespace

// also needed in minimap generation + needs to be outside of local namespace
bool operator<(const Tile &lhs, const Tile &rhs)
{
	return lhs.symbol < rhs.symbol;
}


Map::Map(const short size, const short variation, const short players)
{
	// randomise map size
	mapsize = size;
	short sh = int(size)*variation/100;
	if(sh)
		mapsize += (random() % (2*sh)) - sh;
	
	// make sure there's enough room
	sh = 10*sqrt(players); /* With the maximum amount of players (200), this
		gives 141. */
	if(mapsize < sh)
	{
		mapsize = sh;
		timed_log("Note: getting crowded, mapsize adjusted to "
			+ boost::lexical_cast<string>(mapsize));
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
	// a square with side length a power of to. Figure out smallest possible:
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
	// Determine whether we are making an outdoor map:
	if((outdoor = random()%2)) // best values for h are in the range 0.08--0.2
	{
		h = 0.14f + fractRand(.06f);
		thresholds = od_thresholds;
		tiles = outdoor_tiles;
		num_tile_types = NUM_OD_TILES;
	}
	else // underground, best values for h seem to be 0.3--0.45
	{
		h = 0.375f + fractRand(.075f);
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
	--sh;
    for(i = 0; i < mapsize; ++i)
	{
		for(j = 0; j < mapsize; ++j)
		{
			tp = mod_tile(i, j);
			for(k = 0; k < num_tile_types; ++k)
			{
				// The last test (when k = num_tile_types-1) always
				// succeeds, since the mesh values are -1...1!
				if(mesh[(i*sh)+j] > thresholds[k])
				{
					*tp = tiles[k];
					break;
				}
			}
		}
    }
	delete[] mesh;

	// If we are creating an underground map, we must randomly change
	// the floor tiles to rough tiles:
	if(!outdoor)
	{
		for(i = 0; i < mapsize; ++i)
		{
			for(j = 0; j < mapsize; ++j)
			{
				if(random()%100 < CHANCE_ROUGH && data[j][i] == T_FLOOR)
					data[j][i] = T_ROUGH;
			}
		}
	}

	// Next, we create some houses.
	char num_houses = random()%(6 + 20*mapsize/450);
	inhabited = (num_houses >= 3 + 20*mapsize/900);
	Coords c(mapsize/3 + random()%(mapsize/2), mapsize/3 + random()%(mapsize/2));
	char ch;
	for(; num_houses > 0; --num_houses)
	{
		gen_house(outdoor);
		// That generated a "house pattern" to the table 'house'. Now we
		// must apply this pattern to a location on the actual map.
		// First loop blows up areas around doors&windows:
		for(i = 0; i < MAX_HOUSE_SIZE; ++i)
		{
			for(j = 0; j < MAX_HOUSE_SIZE; ++j)
			{
				ch = house[j*MAX_HOUSE_SIZE+i];
				if(ch == '+')
				{
					if(outdoor)
						add_patch(Coords(c.x+i, c.y+j), T_ROAD);
					else
						add_patch(Coords(c.x+i, c.y+j), T_FLOOR);
				}
				else if(ch == '|' || ch == '-')
					add_patch(Coords(c.x+i, c.y+j), T_GROUND);
			}
		}
		// Second run actually puts the house there:
		for(i = 0; i < MAX_HOUSE_SIZE; ++i)
		{
			if(c.x+i >= mapsize)
				break;
			for(j = 0; j < MAX_HOUSE_SIZE; ++j)
			{
				if(c.y+j >= mapsize)
					break;
				tp = mod_tile(c.x + i, c.y + j);
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

		// Set location for next house:
		c.x += 3*MAX_HOUSE_SIZE/2 + random()%MAX_HOUSE_SIZE;
		c.y += random()%7 - 3;
		if(c.x >= mapsize - MAX_HOUSE_SIZE - 1) // wen't too far
		{
			c.x = 2 + random()%10;
			c.y += 3*MAX_HOUSE_SIZE/2 + random()%MAX_HOUSE_SIZE;
			if(c.y >= mapsize - MAX_HOUSE_SIZE - 1)
				c.y = 2 + random()%10;
		}
	}

	// Always finish with the boundary:
	rowit rit = data.begin();
	vector<Tile>::iterator it;
	for(it = rit->begin(); it != rit->end(); ++it)
		*it = T_WALL;
	rit = data.begin()+mapsize-1;
	for(it = rit->begin(); it != rit->end(); ++it)
		*it = T_WALL;
	for(rit = data.begin(); rit != data.end(); ++rit)
		(*rit)[0] = (*rit)[mapsize-1] = T_WALL;

	// Finally, initialize the BY_WALL flags (this might be doable faster, too)
	// Note that no need to init for the edge since it's undiggable.
	c.y = 1;
	while(c.y < mapsize-1)
	{
		c.x = 1;
		while(c.x < mapsize-1)
		{
			upd_by_wall_flag(c);
			c.x++;
		}
		c.y++;
	}

	/*DEBUG*/
#if 0
	string str;
	for(rowit it = data.begin(); it != data.end(); ++it)
	{
		str.clear();
		for(vector<Tile>::iterator i = it->begin(); i != it->end(); ++i)
		{
			str += i->symbol;
			if(str.size() > 139)
				break;
		}
		cerr << str << endl;
	}
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


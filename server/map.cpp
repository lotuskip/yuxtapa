// Please see LICENSE file.
#include "map.h"
#include "settings.h"
#include "../common/util.h"
#ifndef MAPTEST
#include "log.h"
#include "../common/constants.h"
#include "../common/los_lookup.h"
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
const char EVERY_N_WALL_UNDIG[MAX_MAPTYPE] = { // every Nth wall is made undiggable (in random)
	30, // dungeon
	12, // outdoor
	3 // complex
};

const char CORRIDOR_NARROW_1IN = 3; // every nth corridor narrow
const char TIGHT_SPOT_CHANCE = 25; // every nth spot in a wide corridor narrow

const Tile T_TREE = { TF_WALKTHRU|TF_SEETHRU, 'T', C_TREE };
const Tile T_GROUND = { TF_WALKTHRU|TF_SEETHRU, '.', C_GRASS };
const Tile T_WALL = { 0, '#', C_WALL };
const Tile T_ILLUSION_WALL = { TF_WALKTHRU, '#', C_WALL };
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

const string mapfile_begin_string = "yux+apa mapfile v.2";

char char_from_tile(const Tile& t)
{
	switch(t.symbol)
	{
	case '.': // floor, ground, or road
		if(t.cpair == C_GRASS)
			return t.symbol;
		if(t.cpair == C_ROAD)
			return ',';
		// must be floor
		return '_';
	case '#': // walls
		if(t.flags & TF_NODIG) // undiggable
			return 'H';
		if(t.flags & TF_WALKTHRU) // illusion wall
			return 'I';
		// fall down:
	default:
	/*case 'T': // tree
	case ' ': // chasm
	case '\"': // swamp
	case '~': // water
	case ';': // rough
	case '+': // open door
	case '\\': // closed door
	case '|': case '-': // windows */
		return t.symbol;
	}
}

bool tile_from_char(const char ch, Tile& t)
{
	switch(ch)
	{
	case '.': t = T_GROUND; break;
	case ',': t = T_ROAD; break;
	case '_': t = T_FLOOR; break;
	case '#': t = T_WALL; break;
	case 'H': t = T_WALL; t.flags |= TF_NODIG; break;
	case 'T': t = T_TREE; break;
	case 'I': t = T_ILLUSION_WALL; break;
	case ' ': t = T_CHASM; break;
	case '\"': t = T_SWAMP; break;
	case '~': t = T_WATER; break;
	case ';': t = T_ROUGH; break;
	case '+':
	case '\\': t = T_DOOR; t.symbol = ch; break;
	case '|':
	case '-': t = T_WINDOW; t.symbol = ch; break;
	default: return true; // unrecognized!
	}
	return false;
}

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
	const int size, const int subSize, const float* const fa)
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
	const float* const fa)
{
    return (fa[((i-stride)*size) + j-stride] + fa[((i-stride)*size) + j+stride]
		+ fa[((i+stride)*size) + j-stride] + fa[((i+stride)*size) + j+stride])*.25f;
}

/*
 * Size must be a power of two + 1 (65, 129, 257, 513)
 * h determines smoothness: 0->very jagged, 1->very smooth
 */
void fill2DFractArray(float* const fa, const int size, const float h)
{
    int subSize = size-1;
	float ratio = pow(2.,-h);
	float scale = ratio;
    int stride = subSize/2;

	// Initialize the corner values. This greatly affects the final outcome,
	// so we don't just randomize them (that can lead to maps that are almost
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

/* Once the random abstract fractal (a table of floats!) is created using the
 * above code, we paint it with map tiles, using the following threshold
 * values (each map type separately): */

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

const char NUM_CO_TILES = 4;
const Tile complex_tiles[NUM_CO_TILES] = { T_WALL, T_WATER, T_FLOOR, T_CHASM };
const float co_thresholds[NUM_CO_TILES] = {
	/*1.0...*/0.5f, // wall
	/*...*/0.0f, // water
	/*...*/-0.5f, // floor
	/*...*/-2.0f // chasm
};

///////////////////////////////////////////////////////////////////

// Don't modify these:
const char MAX_HS = 14; // max house size
const char ROOM_NUM = (MAX_HS-2)/3; // 4
const char ROOM_SIZE = 3;

// buffer to hold a house while it's being generated
char house[MAX_HS*MAX_HS];
bool rooms[ROOM_NUM*ROOM_NUM];

const char roomcell[7][9] = {
{ '.', '.', '.',
  '.', '.', '.',
  '.', '.', '.' },
{ '#', '.', '.',
  '#', '.', '.',
  '#', '#', '#' },
{ '#', '.', '#',
  '.', '.', '.',
  '#', '.', '#' },
{ '#', ':', '#',
  '#', '.', '#',
  '#', ':', '#' },
{ '#', ':', '#',
  '#', '.', '#',
  '#', '#', '#' },
{ '.', '.', '.',
  '.', '#', '.',
  '.', '.', '.' },
{ '#', '.', '#',
  '#', '.', '.',
  '#', '#', '#' },
};

char rot_0(const char x, const char y, const char* const p)
{
	return p[y*ROOM_SIZE+x]; // yep, identity map.
}
char rot_90(const char x, const char y, const char* const p)
{
	return p[(ROOM_SIZE-1-x)*ROOM_SIZE+y];
}
char rot_180(const char x, const char y, const char* const p)
{
	return p[(ROOM_SIZE-1-y)*ROOM_SIZE+ROOM_SIZE-1-x];
}
char rot_270(const char x, const char y, const char* const p)
{
	return p[x*ROOM_SIZE+ROOM_SIZE-1-y];
}

char (*rot_fs[4])(const char, const char, const char* const)
	= { rot_0, rot_90, rot_180, rot_270 };


// construct a new "room cell" at (rx,ry), coming from 'dir' (dir==-1: initial cell)
void room_recur(const char rx, const char ry, const char dir)
{
	if(rooms[ry*ROOM_NUM+rx])
		return;
	rooms[ry*ROOM_NUM+rx] = true;
	// See where we can branch from here:
	bool branches[4] = {  (dir != 0 && ry != 0) /*N*/,
		(dir != 1 && rx != ROOM_NUM-1) /*E*/,
		(dir != 2 && ry != ROOM_NUM-1) /*S*/,
		(dir != 3 && rx != 0) /*W*/ };
	char (*rot_func)(const char, const char, const char*) = &rot_0;
	// Make a random kind of room here:
	char room_type; // index to the 'roomcell' table
	// First room (dir == -1) must not be a corridor only, and we favour
	// the non-corridors otherwise, too:
	if(dir == -1 || !(random()%5))
		room_type = random()%2; // 0 or 1 is okay
	else
		room_type = random()%7;
	switch(room_type)
	{
	case 1: case 6: /* #..  #.#  rotate these similarly
			         * #..  #..
			         * ###  ###  */
		if(dir != -1)
		{
			if(random()%2) // dir enters from the top
			{
				rot_func = rot_fs[dir];
				branches[(dir+2)%4] = branches[(dir+3)%4] = false;
			}
			else // dir enters from the side
			{
				rot_func = rot_fs[(dir+3)%4];
				branches[(dir+1)%4] = branches[(dir+2)%4] = false;
			}
		}
		else
			branches[2] = branches[3] = false;
		break;
	case 4: /* #.#  can branch only one way
			 * #.#
			 * ### */
		if(dir != -1)
			rot_func = rot_fs[dir];
		branches[0] = branches[1] = branches[2] = branches[3] = false;
		break;
	case 3: /* #.# can branch N / S
			 * #.#
			 * #.# */
		if(dir%2)
		{
			rot_func = rot_90;
			branches[0] = branches[2] = false;
		}
		else
			branches[1] = branches[3] = false;
		break;
	// default: (Others can branch any way and don't need rotation.)
	}

	// Put the room in place:
	char x, y;
	for(y = 0; y < ROOM_SIZE; ++y)
	{
		for(x = 0; x < ROOM_SIZE; ++x)
			house[(ry*ROOM_SIZE+1+y)*MAX_HS+rx*ROOM_SIZE+1+x]
				= rot_func(x, y, roomcell[room_type]);
	}

	// Branch where-ever we can:
	if(branches[0]) room_recur(rx, ry-1, 2);
	if(branches[1]) room_recur(rx+1, ry, 3);
	if(branches[2]) room_recur(rx, ry+1, 0);
	if(branches[3]) room_recur(rx-1, ry, 1);
}

// Here's the entry point function.
void gen_house(const bool outdoor)
{
	unsigned short sh;
	for(sh = 0; sh < MAX_HS*MAX_HS; ++sh)
		house[sh] = '#';
	for(sh = 0; sh < ROOM_NUM*ROOM_NUM; ++sh)
		rooms[sh] = false;
	
	// This calls itself recursively:
	room_recur(random()%ROOM_NUM, random()%ROOM_NUM, -1);

	// Remove extra (surrounding) walls; any wall that has as neighbours
	// only '#' or '_' becomes '_'. While looping, also place doors and
	// windows.
	Coords c, d;
	string card;
	bool outwall;
	for(c.y = 0; c.y < MAX_HS; ++(c.y))
	{
		for(c.x = 0; c.x < MAX_HS; ++(c.x))
		{
			if(house[c.y*MAX_HS+c.x] == '#')
			{
				card.clear();
				outwall = true;
				for(sh = 0; sh < MAX_D; ++sh)
				{
					d = c.in(e_Dir(sh));
					if(d.x >= 0 && d.y >= 0 && d.x < MAX_HS && d.y < MAX_HS)
					{
						if(house[d.y*MAX_HS+d.x] == '.' || house[d.y*MAX_HS+d.x] == ':')
							outwall = false;
						if(!(sh%2)) // cardinal direction
							card += house[d.y*MAX_HS+d.x];
					}
					else if(!(sh%2))
						card += '_'; // outside of buffer
				}
				if(outwall)
					house[c.y*MAX_HS+c.x] = '_';
				else // See if this is a wall we could place a window on
				{
					// replace any ':' with '.' (there can be at most one)
					if((sh = card.find(':')) != string::npos)
						card[sh] = '.';
					if(card == "_#.#" || card == ".#_#")
						house[c.y*MAX_HS+c.x] = '-';
					else if(card == "#.#_" || card == "#_#.")
						house[c.y*MAX_HS+c.x] = '|';
				}
			}
			// Check if could place a door:
			else if(house[c.y*MAX_HS+c.x] == ':'
				&& random()%3 // skip every 2nd potential door loc
				&& house[(c.y+1)*MAX_HS+c.x] == house[(c.y-1)*MAX_HS+c.x]
				&& house[c.y*MAX_HS+c.x+1] == house[c.y*MAX_HS+c.x-1])
				house[c.y*MAX_HS+c.x] = '+';
		}
	}

	// The windows we placed above were only "preliminary plans". At least one
	// of them must be replaced by our front door, and if we are not outside,
	// there can be no windows!
	//First place a front door:
	outwall = false;
	do {
		sh = random()%(MAX_HS*MAX_HS);
		while(sh)
		{
			if(house[sh] == '|' || house[sh] == '-')
			{
				house[sh] = '+';
				outwall = true;
				break;
			}
			--sh;
		}
	} while(!outwall);
	// Next, remove all or some windows and add more doors:
	char num = 0; // number of additional doors
	for(sh = 0; sh < MAX_HS*MAX_HS; ++sh)
	{
		if(house[sh] == '|' || house[sh] == '-')
		{
			if(num < 3 && !(random()%10)) // another front door
			{
				house[sh] = '+';
				++num;
			}
			else if(!outdoor || random()%4)
				house[sh] = '#';
		}
		// possibly turn some walls into illusions: 
		else if(!outdoor && house[sh] == '#' && !(random()%15))
			house[sh] = 'I';
	}
} // gen_house


///////////////////////////////////////////////////////////////////

/* We'll typically generate a number of houses, and place them using
 * one of the following algorithms. */

// 'circular' places the houses in a circle
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
	// We want the points to be at a *walking* distance of MAX_HS+1:
	short rad = (MAX_HS+1)/max(abs(1.0f-cos(theta)), abs(sin(theta)))+1;
	/* NOTE: the maximum number of houses being limited by map size should
	 * ensure that rad cannot be too large. If rad is too large the houses
	 * will be put outside of the map... */
	if(rad >= size/2 - MAX_HS)
	{
#ifdef DEBUG
		cerr << "Debug warning: circular house placement has too big radius!" << endl;
#endif
		rad = size/2 - MAX_HS - 1;
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
		hc[i].x = hc[i-1].x + MAX_HS + 1 + random()%MAX_HS;
		hc[i].y = hc[i-1].y + random()%7 - 3;
		if(hc[i].x >= size - MAX_HS - 1) // wen't too far
		{
			hc[i].x = 2 + random()%10;
			hc[i].y += MAX_HS + 1 + random()%MAX_HS;
			if(hc[i].y >= size - MAX_HS - 1)
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
	p.x = p.x*2*MAX_HS/(3*len);
	p.y = p.y*2*MAX_HS/(3*len);
	// Determine step (how far "down the street" the next houses are):
	v.x /= num+1;
	if(v.x > 3*MAX_HS/2)
		v.x = 3*MAX_HS/2;
	v.y /= num+1;
	if(v.y > 3*MAX_HS/2)
		v.y = 3*MAX_HS/2;
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
			+ lex_cast(mapsize));
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

	short i, j;
	char k;
	Tile *tp;

	float* mesh = new float[sh*sh]; // required by the fractal algorithm
	float h; // the coarseness factor for the map generation
	const float *thresholds;
	const Tile *tiles;
	char num_tile_types;

	// Determine map type:
#ifndef MAPTEST
	if((maptype = Config::next_map_type()) == MT_OUTDOOR)
#else
	if((maptype = e_MapType(random()%MAX_MAPTYPE)) == MT_OUTDOOR)
#endif
	{
		// best values for h are in the range 0.28--0.34
		h = 0.31f + fractRand(0.03f);
		thresholds = od_thresholds;
		tiles = outdoor_tiles;
		num_tile_types = NUM_OD_TILES;
	}
	else if(maptype == MT_DUNGEON)
	{
		// best values for h seem to be 0.4--0.5
		h = 0.45f + fractRand(0.05f);
		thresholds = ug_thresholds;
		tiles = underground_tiles;
		num_tile_types = NUM_UG_TILES;
	}
	else // complex
	{
		h = 0.6f;
		thresholds = co_thresholds;
		tiles = complex_tiles;
		num_tile_types = NUM_CO_TILES;
	}
	fill2DFractArray(mesh, sh, h);
	// The "fractal" was generated; convert into Tile-representation.
	// Note that we ignore a lot of the generated data here, unless
	// mapsize is a power of 2.
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

	// If we are creating a dungeon map, we add corridors and randomly
	// change the floor tiles to rough tiles:
	if(maptype == MT_DUNGEON)
	{
		// Corridor algorithm; kind of like maze generation:
		sh = random()%9 + 9; // 9-17, 'cell' size in map tiles
		k = mapsize/sh; // number of 'cells'
		Coords cell(random()%k, random()%k), cell2;
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
				if(cell2.x >= 0 && cell2.y >= 0 && cell2.x < k && cell2.y < k
					&& find(mazed.begin(), mazed.end(), cell2) == mazed.end())
					break; // this will do
				++d;
			}
			if(i == MAX_D) // no suitable neighbours available!
			{
				// This means 'cell' is an end of a corridor; make a patch
				// of floor there, possibly:
				if(random()%3)
					add_patch(Coords(cell.x*sh+sh/2, cell.y*sh+sh/2), T_FLOOR);
				// see if have generated enough (4/7 a mapfull):
				if(mazed.size() < 4u*k*k/7u) // no
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
			widen = random()%CORRIDOR_NARROW_1IN;
			cell.x = cell.x*sh + sh/2; // convert cells to middle pt coords
			cell.y = cell.y*sh + sh/2;
			cell2.x = cell2.x*sh + sh/2;
			cell2.y = cell2.y*sh + sh/2;
			while(!(cell == cell2))
			{
				data[cell.y][cell.x] = T_FLOOR;
				if(widen && (random()%TIGHT_SPOT_CHANCE))
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
	} // generating a dungeon

	// Next, we create some houses (except in complex type maps). The map is
	// inhabited if the number of houses is high enough.
	if(maptype != MT_COMPLEX)
	{
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
				for(i = 0; i < MAX_HS; ++i)
				{
					for(j = 0; j < MAX_HS; ++j)
					{
						if((k = house[j*MAX_HS+i]) == '+')
							add_patch(Coords(ci->x+i, ci->y+j),
								(maptype == MT_OUTDOOR) ? T_ROAD : T_FLOOR);
						else if(k == '|' || k == '-')
							add_patch(Coords(ci->x+i, ci->y+j), T_GROUND);
					}
				}
				// Second run actually puts the house there:
				apply_house(*ci);

				hcoords.erase(hcoords.begin());
			} // loop creating houses
		} // if create houses
	} // not creating a complex type map
	else
	{
		// Complex type map; we simply "fill the map with houses".
		for(i = 0; i < mapsize; i += MAX_HS-1)
		{
			for(j = 0; j < mapsize; j += MAX_HS-1)
			{
				gen_house(false);
				apply_house(Coords(i,j));
			}
		}
		inhabited = random()%2;
	}

	// Always finish with the boundary wall, undiggable:
	fix_boundary();

	// Finally, initialize the BY_WALL flags (this might be doable faster, too)
	// and make some walls undiggable. Note: no need to init for the edge
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

#ifdef MAPTEST
	// print out the map (or at least a part of it) to stderr.
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
		if(!point_out_of_map((cn = c.in(d)))
			&& data[cn.y][cn.x] == T_WALL)
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


void Map::apply_house(const Coords &c)
{
	short i, j;
	Tile* tp;
	for(i = 0; i < MAX_HS; ++i)
	{
		if(c.x+i >= mapsize)
			break;
		for(j = 0; j < MAX_HS; ++j)
		{
			if(c.y+j >= mapsize)
				break;
			tp = mod_tile(c.x + i, c.y + j);
			switch(house[j*MAX_HS+i])
			{
			case ':': case '.':
				*tp = T_FLOOR; break;
			case '#':
				*tp = T_WALL; break;
			case 'I':
				*tp = T_ILLUSION_WALL; break;
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
				tp->symbol = house[j*MAX_HS+i];
				break;
			// default: do nothing
			}
		}
	}
}


// Create a "circular" (the "wrong circular") patch with Tile t at c, rad. 2-4
void Map::add_patch(const Coords &c, const Tile &t)
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


// Assures the boundary of the map is all undiggable wall
void Map::fix_boundary()
{
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
	if(t.symbol == '#') // includes illusory walls
		return T_WALL;
	return t;
}


void Map::gen_miniview(char *target) const
{
	short x, y, ax, ay, F;
	map<Tile, short> amounts;
	map<Tile, short>::iterator it;
	Tile t;
	
	// sample F*F map tiles for each miniview tile
	F = mapsize/VIEWSIZE + 1;
	while(VIEWSIZE*F >= mapsize)
		--F;

	for(y = 0; y < VIEWSIZE; ++y)
	{
		for(x = 0; x < VIEWSIZE; ++x)
		{
			// collect tile amounts in this area:
			for(ay = y*F; ay < (y+1)*F; ++ay)
			{
				for(ax = x*F; ax < (x+1)*F; ++ax)
					amounts[data[ay][ax]]++;
			}
			// Do some reduction. Think of this as a "crude sinc filter"
			for(it = amounts.begin(); it != amounts.end(); ++it)
			{
				// The reference value here is the fact that there are a total
				// of F*F tiles!
				if(it->second < 5*F*F/24
					&& (t = subsample_tile(it->first)) != it->first)
				{
					amounts[t] += it->second;
					amounts.erase(it);
					it = amounts.begin(); // start over!
				}
			}
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


// NOTE: this works for radii <= 6. With higher radii you need more intensive
// tests (cf. LOS algorithm in viewpoint.cpp)
bool Map::LOS_between(const Coords& c1, const Coords& c2, const char maxrad,
	const short calced_rad)
{
	short r = (calced_rad == -1) ? c2.dist_walk(c1) : calced_rad;
	if(r <= maxrad) // close enough
	{
		// Check if there is a LOS between c1 and c2:
		if(r <= 1)
			return true; // seen, done
		// Construct vector from c1 to c2:
		Coords v(c2.x - c1.x, c2.y - c1.y);
		char line, ind;
		if(v.y == -r) line = v.x + r;
		else if(v.x == r) line = 3*r + v.y;
		else if(v.y == r) line = 5*r - v.x;
		else if(v.x == -r) line = 7*r - v.y;
		else /* v.x == -r */ line = 7*r - v.y;
		for(ind = 0; ind < 2*r; ind += 2)
		{
			if(!(data[loslookup[r-2][line*2*r+ind+1]+c1.y]
				[loslookup[r-2][line*2*r+ind]+c1.x].flags & TF_SEETHRU))
				break;
		}
		if(ind == 2*r) // got all the way!
			return true; // seen, done
		// else, need to check the opposite direction, the line from c2 to c1
		// (but only if the line is one of the unsymmetric ones):
		if(calced_rad == -1 && line%r)
			return LOS_between(c2, c1, maxrad, r);
	}
	return false;
}


bool Map::load_from_file(const string &mapname)
{
	ifstream file((Config::get_mapdir() + mapname).c_str());
	if(!file)
		return false;
	// else:
	string s;
	getline(file, s);
	if(s != mapfile_begin_string)
		return false; // the map is of a different version; can't be certain it will work
	// else:
	getline(file, s); // (skip comment line)
	file >> mapsize;
	if(mapsize < MIN_MAP_SIZE || mapsize > MAX_MAP_SIZE)
		return false; // a corrupt file or something
	// else
	file >> inhabited; // this should work regardless of locale?
	file.seekg(1, ios_base::cur); // skip newline
	getline(file, s);
	char ch;
	for(ch = 0; ch < MAX_MAPTYPE; ++ch)
	{
		if(s == short_mtype_name[ch])
		{
			maptype = e_MapType(ch);
			break;
		}
	}
	if(ch == MAX_MAPTYPE)
		return false; // no recognized maptype found
		
	make_right_size(data);
	vector<Tile>::iterator i;
	short ind;	
	for(rowit it = data.begin(); it != data.end(); ++it)
	{
		getline(file, s);
		if(s.size() != (unsigned short)mapsize)
			return false; // bogus data!
		ind = 0;
		for(i = it->begin(); i != it->end(); ++i)
		{
			if(tile_from_char(s[ind], *i))
				return false; // bogus data
			++ind;
		}
		if(!file)
			return false; // file corrupt or something else wrong
	}

	// Manually edited maps might have something else than undiggable wall on
	// the edges:
	fix_boundary();
	// Update BY_WALLs:
	Coords c;
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
	return true;
}
#endif // not maptest build


bool Map::save_to_file(const std::string &mapname) const
{
	ofstream file((Config::get_mapdir() + mapname).c_str());
	if(!file)
		return false;
	// else:
	file << mapfile_begin_string << endl;

	time_t rawtime;
	struct tm *timeinfo;
	char buffer[17]; // eg. "01/02/2011 12:13" (and '\0')
	time(&rawtime);
 	timeinfo = localtime(&rawtime);
	strftime(buffer, 17, "%d/%m/%Y %H:%M", timeinfo);
	file << "Saved on: " << buffer << endl;

	file << mapsize << endl;
	// force using "1" for true and "0" for false; can't trust locales:
	if(inhabited)
		file << '1';
	else
		file << '0';
	file << endl;
	file << short_mtype_name[maptype] << endl;

	vector<Tile>::const_iterator i;	
	for(vector< vector<Tile> >::const_iterator it = data.begin(); it != data.end(); ++it)
	{
		for(i = it->begin(); i != it->end(); ++i)
			file << char_from_tile(*i);
		file << endl;
	}
	return true;
}


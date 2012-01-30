// Please see LICENSE file.
#ifndef MAPTEST
#include "actives.h"
#include "game.h"
#include "map.h"
#include "spiral.h"
#include "../common/los_lookup.h"
#include "../common/util.h"
#include <algorithm>
#ifdef DEBUG
#include <iostream>
#endif

extern e_GameMode gamemode;
extern e_Dir obj_sector; // defined in game.cpp

namespace
{
using namespace std;

const char CARRIED_TORCH_RAD = 6;

const short BIG_MAP_LIM = 150;

// constants for placement:
const short TRAP_ONE_IN = 85;
const short BLOCK_ONE_IN = 190;
const char TORCH_GRID = 17;
const char CHANCE_NO_TORCH = 30;
const char NUM_DESTROY_BLOCKS = 10;
const char CHANCE_PORTALS = 45;
const char PORTALS_FROM_EDGE = 15;

// placement can take place if the tile has one of these symbols:
const string all_placem_syms = ".\"^T";
void next_unused_tile(Coords &c, vector<Coords> &useds)
{
	init_nearby(c);
	// Find next (A) unused, and (B) free spot:
	while(find(useds.begin(), useds.end(), c) != useds.end()
		|| all_placem_syms.find(Game::curmap->get_tile(c).symbol) == string::npos)
		c = next_nearby();
	// Found it:
	useds.push_back(c);
}

// traps are generated separately (they have varying colour), and corpses
// are only added in-game:
const char noccent_symbol[MAX_NOE-1] = { '_', 'A', 'V', '0', '&', 't' };
const char noccent_cpair[MAX_NOE-1] = { C_WALL, C_PORTAL_IN, C_PORTAL_OUT,
	C_PORTAL_IN, C_NEUT_FLAG, C_TORCH };
void add_noccent(const Coords &c, const e_Noccent type, const short m = 0)
{
	noccents[type].push_back(NOccEnt(c, noccent_symbol[type-1],
		noccent_cpair[type-1], m));
	Tile* tp = Game::curmap->mod_tile(c);
	// A non-occupying entity can be placed in almost any square, but we should
	// ensure that that square won't have "hidden effects":
	if(tp->symbol != '.') // Some "special" tile; replace with ever-safe floor:
		*tp = T_FLOOR;
	tp->flags |= TF_NOCCENT;

	// Special things when placing team-owned flags:
	if(type == NOE_FLAG && m != T_NO_TEAM)
	{
		noccents[NOE_FLAG].back().set_col(team_colour[m]);
		team_flags[m].push_back(--(noccents[NOE_FLAG].end()));
		// The surroundings of a team flag should be open enough:
		e_Dir d = D_N;
		char obss = 0; // 'obstructions', wall or water or chasm
		do {
			tp = Game::curmap->mod_tile(c.in(d));
			if(!(tp->flags & TF_WALKTHRU) || (tp->flags & (TF_KILLS|TF_DROWNS)))
				++obss;
			++d;
		} while(d != D_N);
		if(obss >= 3) // too many!
		{
			// Replace them all with floor:
			d = D_N;
			Coords cn;
			short msize = Game::curmap->get_size();
			do {
				cn = c.in(d);
				// Do not replace outer wall, though:
				if(cn.x > 1 && cn.y > 1 && cn.x < msize-1 && cn.y < msize-1)
					*(Game::curmap->mod_tile(cn)) = T_FLOOR;
				++d;
			} while(d != D_N);
			// Need to update BY_WALL for neighbours of all cns:
			for(cn.y = max(1, c.y-2); cn.y <= min(msize-2, c.y+2); ++cn.y)
			{
				for(cn.x = max(1, c.x-2); cn.x <= min(msize-2, c.x+2); ++cn.x)
					Game::curmap->upd_by_wall_flag(cn);
			}
		}
	}
}

void add_block(const Coords &c)
{
	boulders.push_back(OccEnt(c, 'O', C_WALL));
	Game::curmap->mod_tile(c)->flags |= TF_OCCUPIED;
}

} // end local namespace


list<NOccEnt> noccents[MAX_NOE];
list<Trap> traps;
list<OccEnt> boulders;
list<Arrow> arrows;
list<MM> MMs;
list<Zap> zaps;
list<PCEnt> PCs;

list<list<NOccEnt>::iterator> team_flags[2];

map<Coords, unsigned char> axn_indicators;
map<Coords, e_Sound> reg_sounds;
map<Coords, string> voices;

list<Coords> fball_centers;
set<Coords> event_set;

list<Player>::const_iterator pl_with_item;
bool item_moved;
NOccEnt the_item(Coords(0,0), '=', C_LIGHT_TRAP, 0);


bool events_around(const Coords &pos)
{
	for(set<Coords>::const_iterator it = event_set.begin();
		it != event_set.end(); ++it)
	{
		if(it->dist_walk(pos) <= VIEWSIZE/2)
			return true;
	}
	return false;
}


void do_placement()
{
	short tmp;
	// empty previous stuff
	for(tmp = 0; tmp < MAX_NOE; ++tmp)
		noccents[tmp].clear();
	boulders.clear();
	PCs.clear();
	team_flags[T_GREEN].clear();
	team_flags[T_PURPLE].clear();

	// we'll be using the nearby search in placement; see spiral.h
	short msize = Game::curmap->get_size();
	nearby_set_dim(msize);

	vector<Coords> used_coords;
	Coords c, d;
	// First place blocks (occent) and traps (noccent) in a single loop:
	// anywhere on a floor/ground/road tile
	for(c.y = 0; c.y < msize; c.y++)
	{
		for(c.x = 0; c.x < msize; c.x++)
		{
			// traps & blocks go only at open spaces:
			if(Game::curmap->get_tile(c).symbol == '.')
			{
				if(!(random()%TRAP_ONE_IN))
				{
					// random trap type:
					traps.push_back(Trap(c, random()%MAX_TRAP_TYPE, cur_players.end()));
					Game::curmap->mod_tile(c)->flags |= TF_TRAP;
					used_coords.push_back(c);
				}
				if(!(random()%BLOCK_ONE_IN))
				{
					add_block(c);
					// we might end up adding the same coords twice, but that
					// doesn't matter:
					used_coords.push_back(c);
				}
			}
		}
	}
	// Torches in a grid, but not if uninhabited
	if(Game::curmap->is_inhabited())
	{
		for(c.y = TORCH_GRID/2; c.y < msize; c.y += TORCH_GRID)
		{
			for(c.x = TORCH_GRID/2; c.x < msize; c.x += TORCH_GRID)
			{
				if(random()%100 >= CHANCE_NO_TORCH)
				{
					d = c;
					next_unused_tile(d, used_coords);
					add_noccent(d, NOE_TORCH);
				}
			}
		}
	}
	// Flags.
	// One green flag in a random corner:
	e_Dir green_corner = e_Dir(1 + 2*(random()%3));
	d = Game::curmap->get_center_of_sector(green_corner);
	next_unused_tile(d, used_coords);
	add_noccent(d, NOE_FLAG, T_GREEN);
	// A purple flag at the opposite side of the map:
	e_Dir purple_corner = !green_corner;
	// possibly rotate so that this flag won't be in a corner:
	switch(random()%3)
	{
	case 0: ++purple_corner; break;
	case 1: --purple_corner; break;
	// case 2: nothing
	}
	d = Game::curmap->get_center_of_sector(purple_corner);
	next_unused_tile(d, used_coords);
	add_noccent(d, NOE_FLAG, T_PURPLE);
	/* The result so far is either
	 * G..      G..
	 * ...   or ...   (or some rotated or mirrored version of these)
	 * ..P      .P.
	 */
	// Some more flags depending on mapsize & game mode:
	e_Dir tmpd;
	switch(gamemode)
	{
	case GM_DESTR:
	case GM_DOM:
		// Two neutral flags more always:
		tmpd = green_corner;
		++tmpd;
		if(random()%2) ++tmpd;
		d = Game::curmap->get_center_of_sector(tmpd);
		next_unused_tile(d, used_coords);
		add_noccent(d, NOE_FLAG, T_NO_TEAM);
		/* This makes it either
		 * GN.       G.N
		 * ..p   or  ..p   (or some rotation/mirroring of these,
		 * .pp       .pp    + the purple flag in one of the p:s)
		 */
		tmpd = green_corner;
		--tmpd;
		if(random()%2) --tmpd;
		d = Game::curmap->get_center_of_sector(tmpd);
		next_unused_tile(d, used_coords);
		add_noccent(d, NOE_FLAG, T_NO_TEAM);
		/* And that added another 'N' on the other side of 'G' */
		// On large maps, one more in central area:
		if(msize >= BIG_MAP_LIM)
		{
			d.y = d.x = msize/2;
			next_unused_tile(d, used_coords);
			add_noccent(d, NOE_FLAG, T_NO_TEAM);
		}
		break;
	case GM_CONQ:
		// add one purple-own 3rd flag in central area:
		d.y = d.x = msize/2;
		next_unused_tile(d, used_coords);
		add_noccent(d, NOE_FLAG, T_PURPLE);
		break;
	case GM_STEAL:
		/* Give the purples another flag, and also position the item here.
		 * It's going to be:
		 * GN.       G..
		 * .Pi   or  N.P    (where 'i' is the item)
		 * ..P       .Pi
		 */
		if(!(int(purple_corner)%2)) // existing P is not in the corner
		{
			// item:
			tmpd = !green_corner;
			d = Game::curmap->get_center_of_sector(tmpd);
			next_unused_tile(d, used_coords);
			the_item.setpos(d);
			obj_sector = tmpd;
			// flag:
			if(++tmpd == purple_corner)
				--(--tmpd);
			d = Game::curmap->get_center_of_sector(tmpd);
			next_unused_tile(d, used_coords);
			add_noccent(d, NOE_FLAG, T_PURPLE);
		}
		else // purple flag is in the corner
		{
			// flag:
			d = Game::curmap->get_center_of_sector(MAX_D);
			next_unused_tile(d, used_coords);
			add_noccent(d, NOE_FLAG, T_PURPLE);
			// item:
			tmpd = !green_corner;
			if(random()%2) ++tmpd;
			else --tmpd;
			d = Game::curmap->get_center_of_sector(tmpd);
			next_unused_tile(d, used_coords);
			the_item.setpos(d);
			obj_sector = tmpd;
		}
		// Give the greens a neutral vantage-point (always):
		tmpd = green_corner;
		if(random()%2) ++tmpd;
		else --tmpd;
		d = Game::curmap->get_center_of_sector(tmpd);
		next_unused_tile(d, used_coords);
		add_noccent(d, NOE_FLAG, T_NO_TEAM);
		break;
	case GM_TDM: break; // no additional flags
	} // switch game-mode (for flag positioning)
	//
	// Portals:
	// The possibilities are:
	// 2 portals:
	//      * 2 two-ways
	//      * 1 entry, 1 exit
	// 3 portals:
	//      * 3 two-ways
	//      * 2 entries, 1 exit
	//      * 1 entry, 2 exits
	// 	4 portals:
	//      * 4 two-ways
	//      * 2 two-ways, 1 entry, 1 exit
	//      * 1+n entries, 3-n exits (n=0..2)
	// 1. Determine the number of portals to place, if any.
	if(random()%100 < CHANCE_PORTALS)
	{
		vector<Coords> portals(2+random()%3);
		// 2. Collect the coordinates for the portals.
		// First one is just a random point:
		portals[0].x = random()%msize;
		portals[0].y = random()%msize;
		// Second one in the corner furthest away from the first:
		if(portals[0].x < msize/2)
			portals[1].x = msize - 2 - random()%PORTALS_FROM_EDGE;
		else
			portals[1].x = 2 + random()%PORTALS_FROM_EDGE;
		if(portals[0].y < msize/2)
			portals[1].y = msize - 2 - random()%PORTALS_FROM_EDGE;
		else
			portals[1].y = 2 + random()%PORTALS_FROM_EDGE;
		// Third one, if needed:
		if(portals.size() > 2)
		{
			portals[2].x = (portals[0].x + portals[1].x)/2;
			portals[2].y = (portals[0].y + portals[1].y)%msize;
			// Fourth one, if needed:
			if(portals.size() > 3)
			{
				portals[3].x = (portals[0].x + portals[1].x)%msize;
				portals[3].y = (portals[0].y + portals[1].y)/2;
			}
		}
		// 3. Determine what kind of portals to place in these coordinates (above)
		bool twoways = random()%2;
		// place at "the last and second to last coordinates" (either
		// 2 two-ways or 1 entry and 1 exit
		d = portals[portals.size()-1];
		next_unused_tile(d, used_coords);
		if(twoways)
			add_noccent(d, NOE_PORTAL_2WAY);
		else
			add_noccent(d, NOE_PORTAL_ENTRY);
		d = portals[portals.size()-2];
		next_unused_tile(d, used_coords);
		if(twoways)
			add_noccent(d, NOE_PORTAL_2WAY);
		else
			add_noccent(d, NOE_PORTAL_EXIT);
		// If there are 3 portals, add a third two-way, an entry, or an exit:
		if(portals.size()==3)
		{
			d = portals[0];
			next_unused_tile(d, used_coords);
			if(twoways)
				add_noccent(d, NOE_PORTAL_2WAY);
			else if(random()%2)
				add_noccent(d, NOE_PORTAL_ENTRY);
			else
				add_noccent(d, NOE_PORTAL_EXIT);
		}
		else if(portals.size()==4)
		{
			// We have so far either (A) 2 two-ways or (B) 1 one-way entry and 1 exit.
			// In case (A), may add 2 more two-ways or 1 entry & 1 exit.
			// In case (B) may add 2 more two-ways or 2 elements from {entry, exit}.
			// Hence, check if we want more 2-ways:
			if(random()%2) // yes!
			{
				for(char ch = 0; ch < 2; ++ch)
				{
					d = portals[ch];
					next_unused_tile(d, used_coords);
					add_noccent(d, NOE_PORTAL_2WAY);
				}
			}
			else if(twoways) // need 1 entry and 1 exit
			{
				d = portals[0];
				next_unused_tile(d, used_coords);
				add_noccent(d, NOE_PORTAL_ENTRY);
				d = portals[1];
				next_unused_tile(d, used_coords);
				add_noccent(d, NOE_PORTAL_EXIT);
			}
			else // may add freely entries/exits
			{
				for(char ch = 0; ch < 2; ++ch)
				{
					d = portals[ch];
					next_unused_tile(d, used_coords);
					if(random()%2)
						add_noccent(d, NOE_PORTAL_ENTRY);
					else
						add_noccent(d, NOE_PORTAL_EXIT);
				}
			}
		} // four portals
	}
	// Blocks for block destroying objective:
	if(gamemode == GM_DESTR)
	{
		// put blocks in a sector not too close to the green flag
		do
			obj_sector = e_Dir(random()%(MAX_D+1));
		while(obj_sector == green_corner);
		c = Game::curmap->get_center_of_sector(obj_sector);
		for(char j = 0; j < NUM_DESTROY_BLOCKS; ++j)
		{
			d = c;
			// These blocks can go anywhere where other entities can:
			next_unused_tile(d, used_coords);
			add_block(d);
		}
	}
	// Block sources (0-2 at random locations)
	for(tmp = random()%3; tmp > 0; --tmp)
	{
		c.x = 5 + random()%(Game::curmap->get_size()-10);
		c.y = 5 + random()%(Game::curmap->get_size()-10);
		next_unused_tile(c, used_coords);
		add_noccent(c, NOE_BLOCK_SOURCE);
	}	
}

void add_action_ind(const Coords &c, const e_Action a)
{
	Game::curmap->mod_tile(c)->flags |= TF_ACTION;
	event_set.insert(c);
	axn_indicators[c] = 5*NUM_ACTIONS + a; // will linger for 5 turns
}

void clear_action_inds()
{
	map<Coords, unsigned char>::iterator it = axn_indicators.begin();
	while(it != axn_indicators.end())
	{
		if(it->second < NUM_ACTIONS)
		{
			Game::curmap->mod_tile(it->first)->flags &= ~(TF_ACTION);
			event_set.insert(it->first);
			axn_indicators.erase(it++);
		}
		else
		{
			it->second -= NUM_ACTIONS;
			++it;
		}
	}
}


void add_sound(const Coords &c, const e_Sound s)
{
	reg_sounds[c] = s;
	event_set.insert(c);
	Game::curmap->mod_tile(c)->flags |= TF_SOUND;
}

void add_voice(const Coords &c, const string &s)
{
	voices[c] = s;
	event_set.insert(c);
	Game::curmap->mod_tile(c)->flags |= TF_SOUND;
}

void clear_sounds()
{
	for(map<Coords, e_Sound>::const_iterator sit = reg_sounds.begin();
		sit != reg_sounds.end(); ++sit)
	{
		Game::curmap->mod_tile(sit->first)->flags &= ~(TF_SOUND);
		event_set.insert(sit->first);
	}
	reg_sounds.clear();
	for(map<Coords, string>::const_iterator vit = voices.begin();
		vit != voices.end(); ++vit)
	{
		Game::curmap->mod_tile(vit->first)->flags &= ~(TF_SOUND);
		event_set.insert(vit->first);
	}
	voices.clear();
}


void update_static_light(const Coords &c)
{
	char line, ind;
	Coords d;
	// see los_lookup.h
	for(line = 0; line < TORCH_LIGHT_RADIUS*8; ++line)
	{
		for(ind = 0; ind < 2*TORCH_LIGHT_RADIUS; ind += 2)
		{
			d.x = loslookup[TORCH_LIGHT_RADIUS-2][line*2*TORCH_LIGHT_RADIUS+ind] + c.x;
			d.y = loslookup[TORCH_LIGHT_RADIUS-2][line*2*TORCH_LIGHT_RADIUS+ind+1] + c.y;
			if(Game::curmap->point_out_of_map(d))
				break;
			//else
			Game::curmap->mod_tile(d)->flags |= TF_LIT;
			if(!(Game::curmap->mod_tile(d)->flags & TF_SEETHRU))
				break; // next line
		}
	}
	// the LOS-algorithm doesn't consider the position itself:
	Game::curmap->mod_tile(c)->flags |= TF_LIT;	
}


// NOTE: rendering happens right after removing voided entities,
// and thus we don't need to check for the entities being voided
// in the below rendering routines!

bool render_occent_at(const Coords &c, char *target, const bool lit)
{
	for(list<OccEnt>::const_iterator b_it = boulders.begin();
		b_it != boulders.end(); ++b_it)
	{
		if(b_it->getpos() == c)
		{
			b_it->draw(target, lit);
			return true;
		}
	}
	for(list<Arrow>::const_iterator a_it = arrows.begin();
		a_it != arrows.end(); ++a_it)
	{
		if(a_it->getpos() == c)
		{
			a_it->draw(target, lit);
			return true;
		}
	}
	// NOTE: passing false to mms and zaps since they are self-lighting and
	// thus have no separate "lit colour"
	for(list<MM>::const_iterator m_it = MMs.begin(); m_it != MMs.end(); ++m_it)
	{
		if(m_it->getpos() == c)
		{
			m_it->draw(target, false);
			return true;
		}
	}
	for(list<Zap>::const_iterator z_it = zaps.begin(); z_it != zaps.end(); ++z_it)
	{
		if(z_it->getpos() == c)
		{
			z_it->draw(target, false);
			return true;
		}
	}
	return false;
}


void render_noccent_at(const Coords &c, char *target, const bool lit)
{
	list<NOccEnt>::const_iterator it;
	// NOTE: do not draw traps here.
	for(char i = NOE_CORPSE; i < NOE_TORCH; ++i)
	{
		for(it = noccents[i].begin(); it != noccents[i].end(); ++it)
		{
			if(it->getpos() == c)
			{
				it->draw(target, lit);
				return;
			}
		}
	}
	// NOTE: passing false to torches always since they are self-lighting and
	// thus have no separate "lit colour"
	for(it = noccents[NOE_TORCH].begin(); it != noccents[NOE_TORCH].end(); ++it)
	{
		if(it->getpos() == c)
		{
			it->draw(target, false);
			return;
		}
	}
}


bool is_dynlit(const Coords &c)
{
	// A tile is dynamically lit <=> there is a lighting OccEnt close to it,
	// in LOS. (There is no such thing as a "dynamic light" entity.)
	for(list<PCEnt>::const_iterator pc_it = PCs.begin(); pc_it != PCs.end(); ++pc_it)
	{
		if(!pc_it->isvoid() && pc_it->torch_is_lit()
			&& Game::curmap->LOS_between(c, pc_it->getpos(), CARRIED_TORCH_RAD))
			return true;
	}
	for(list<MM>::const_iterator m_it = MMs.begin(); m_it != MMs.end(); ++m_it)
	{
		if(!m_it->isvoid()
			&& Game::curmap->LOS_between(c, m_it->getpos(), 1))
			return true;
	}
	for(list<Zap>::const_iterator z_it = zaps.begin(); z_it != zaps.end(); ++z_it)
	{
		if(!z_it->isvoid()
			&& Game::curmap->LOS_between(c, z_it->getpos(), 1))
			return true;
	}
	for(list<Coords>::const_iterator fb_it = fball_centers.begin(); 
		fb_it != fball_centers.end(); ++fb_it)
	{
		if(Game::curmap->LOS_between(c, *fb_it, 4)) // fireballs light a rad 4
			return true;
	}
	return false;
}


list<PCEnt>::iterator any_pc_at(const Coords &c)
{
	list<PCEnt>::iterator it;
	for(it = PCs.begin(); it != PCs.end(); ++it)
	{
		if(it->getpos() == c && !it->isvoid())
			break;
	}
	return it;
}

list<NOccEnt>::iterator any_noccent_at(const Coords &c, const e_Noccent type)
{
	list<NOccEnt>::iterator it;
	for(it = noccents[type].begin(); it != noccents[type].end(); ++it)
	{
		// actually, most noccents are never voided, but we make the check for all
		if(it->getpos() == c && !it->isvoid())
			break;
	}
	return it;
}

list<Trap>::iterator any_trap_at(const Coords &c)
{
	list<Trap>::iterator it;
	for(it = traps.begin(); it != traps.end(); ++it)
	{
		if(it->getpos() == c && !it->isvoid())
			break;
	}
	return it;
}

list<OccEnt>::iterator any_boulder_at(const Coords &c)
{
	list<OccEnt>::iterator it;
	for(it = boulders.begin(); it != boulders.end(); ++it)
	{
		if(it->getpos() == c && !it->isvoid())
			break;
	}
	return it;
}

OwnedEnt* any_missile_at(const Coords &c)
{
	for(list<Arrow>::iterator a_it = arrows.begin(); a_it != arrows.end(); ++a_it)
	{
		if(!a_it->isvoid() && a_it->getpos() == c)
			return &(*a_it);
	}
	for(list<MM>::iterator m_it = MMs.begin(); m_it != MMs.end(); ++m_it)
	{
		if(!m_it->isvoid() && m_it->getpos() == c)
			return &(*m_it);
	}
	for(list<Zap>::iterator z_it = zaps.begin(); z_it != zaps.end(); ++z_it)
	{
		if(!z_it->isvoid() && z_it->getpos() == c)
			return &(*z_it);
	}
	return 0;
}
#endif // not maptest build


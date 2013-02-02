// Please see LICENSE file.
#ifndef MAPTEST
#include "occent.h"
#include "chores.h"
#include "map.h"
#include "actives.h"
#include "../common/los_lookup.h"
#include <cstdlib>
# ifdef DEBUG
#include <iostream>
# endif

namespace
{
using namespace std;

const char arrowpath_sym[] = "|/-\\|/-\\";
const char zappath_sym[] = "^^>>vv<<";
/* \|/   <^^
 * - -   < >
 * /|\   vv>
 */

const char ARROW_TILES_PER_TURN = 3;
const char ZAP_TILES_PER_TURN = 2;

const char MAX_ZAP_BOUNCES = 7;
const char ZAP_TRAVEL_DIST = 6;
const char MAX_MM_HOMING_DIST = 15;

const char MM_HIT_TREE_1IN = 4;
}

namespace Game { extern Map* curmap; }


OccEnt::OccEnt(const Coords &c, const char sym, const char cp)
	: Ent(c, sym, cp)
{
}


OwnedEnt::OwnedEnt(const Coords &c, const char sym, const char cp,
		const std::list<Player>::iterator o)
	: OccEnt(c, sym, cp), owner(o)
{
}


PCEnt::PCEnt(const Coords &c, const std::list<Player>::iterator o)
	: OwnedEnt(c, '@', C_GREEN_PC, o), invis_to_team(T_NO_TEAM),
	seen_by_team(T_NO_TEAM), torch_lit(false), disguised(false)
{
	if(o->team == T_PURPLE)
		set_col(C_PURPLE_PC);
}


void PCEnt::update() {}


void PCEnt::toggle_torch()
{
	torch_lit = !torch_lit;
	owner->needs_state_upd = true;
}



Arrow::Arrow(Coords t, const list<Player>::iterator o)
	: OwnedEnt(o->own_pc->getpos(), '|', C_ARROW, o)
{
	char r = t.dist_walk(Coords(0,0));
	if(r == 1)
		path.push_back(Coords(t.x+pos.x, t.y+pos.y)); // and done
	else
	{
		char line, ind;
		// see common/los_lookup.h
		if(t.y == -r) line = t.x + r;
		else if(t.x == r) line = 3*r + t.y;
		else if(t.y == r) line = 5*r - t.x;
		else /* t.x == -r */ line = 7*r - t.y;
		for(ind = 0; ind < 2*r; ind += 2)
			path.push_back(Coords(loslookup[r-2][line*2*r+ind] + pos.x,
				loslookup[r-2][line*2*r+ind+1] + pos.y));
	}
	pos = Coords(0,0); // indicates the arrow isn't anywhere yet
	init_path_len = path.size();
}


void Arrow::update()
{
	if(madevoid)
		return;
	event_set.insert(pos); // in any event, arrow moves away
	Game::curmap->mod_tile(pos)->flags &= ~(TF_OCCUPIED);

	for(char ch = 0; ch < ARROW_TILES_PER_TURN; ++ch)
	{
		if(missile_coll(this, path.front()))
			return; // missile_coll should have voided *this
		// else
		pos = path.front();
		path.pop_front();
		if(path.empty()) // this is where the arrow falls
		{
			arrow_fall(this, pos);
			makevoid(); // occupied flag has been unset
			return;
		}
	}
	// Flew all the way without hitting anything; update for drawing:
	Game::curmap->mod_tile(pos)->flags |= TF_OCCUPIED;
	event_set.insert(pos);
	symbol = arrowpath_sym[pos.dir_of(path.front())];
}

bool Arrow::hit_tree()
{
	// chance to hit trees depends on distance travelled
	return random()%100 < (init_path_len - (signed int)path.size() - 2)*60/9;
	/* This gives a chance of 0% when 2 or less tiles have been travelled, a
	 * chance of 60% when 11 tiles have been travelled, and linearly in
	 * between. */
}


MM::MM(const std::list<Player>::iterator o, const e_Dir dir)
	: OwnedEnt(o->own_pc->getpos(), '*', C_MM1, o), lmd(dir), turns_moved(0)
{
	pos = pos.in(lmd);
	Game::curmap->mod_tile(pos)->flags |= TF_OCCUPIED;
}


void MM::update()
{
	if(madevoid)
		return;
	// Don't move on first turn (that's practically always the same turn we're created)
	if(!turns_moved)
	{
		++turns_moved;
		return;
	}
	// else
	event_set.insert(pos); // in any event, moves away
	Game::curmap->mod_tile(pos)->flags &= ~(TF_OCCUPIED);

	if(turns_moved < 2)
		++turns_moved; // and don't change lmd
	else
	{
		// Find the position of the nearest enemy:
		char best_dist = MAX_MM_HOMING_DIST;
		Coords targetc;
		for(list<PCEnt>::iterator it = PCs.begin(); it != PCs.end(); ++it)
		{
			// not void, enemy, closer:
			if(!it->isvoid() && it->get_owner()->team != owner->team
				&& it->getpos().dist_walk(pos) < best_dist)
			{
				targetc = it->getpos();
				best_dist = it->getpos().dist_walk(pos);
			}
		}
		if(best_dist < MAX_MM_HOMING_DIST) // a target was found!
		{
			e_Dir tdir = pos.dir_of(targetc);
			if(tdir == !lmd) // target is right behind us; turn randomly
			{
				if(random()%2) ++lmd;
				else --lmd;
			}
			else // turn lmd "towards" tdir
			{
				if(tdir > lmd)
				{
					if(tdir - lmd <= 3) ++lmd;
					else --lmd;
				}
				else /*tdir < lmd*/ if(lmd - tdir <= 3) --lmd;
				else ++lmd;
			}
		}
		// else just go straight on
	}

	Coords newpos = pos.in(lmd);
	if(missile_coll(this, newpos))
		return; // missile_coll should have voided *this
	// else
	event_set.insert((pos = newpos));
	Game::curmap->mod_tile(pos)->flags |= TF_OCCUPIED;

	cpair = (cpair == C_MM1) ? C_MM2 : C_MM1;
}

bool MM::hit_tree()
{
	// fixed chance:
	return !(random() % MM_HIT_TREE_1IN);
}


Zap::Zap(const std::list<Player>::iterator o, const e_Dir d)
	: OwnedEnt(o->own_pc->getpos(), ' ', C_ZAP, o), dir(d), bounces(-1), travelled(0)
{
}


void Zap::update()
{
	if(madevoid)
		return;
	if(bounces == -1) // indicates the first call to update()
		++bounces;
	else // not the first move; should vacate position:
	{
		event_set.insert(pos);
		Game::curmap->mod_tile(pos)->flags &= ~(TF_OCCUPIED);
	}

	for(char ch = 0; ch < ZAP_TILES_PER_TURN; ++ch)
	{
		if(missile_coll(this, pos.in(dir)))
		{
			// For zaps, missile_coll returns true if we (A) have been voided
			// by hitting something, or (B) hit a PC and _should_ void ourselves
			// if that was the final tile for this round.
			if(isvoid())
				return; // *this voided, possibly via a returning bounce() call
			//else
			if(ch == ZAP_TILES_PER_TURN-1) // hit something and are at the last tile
			{
				makevoid(); // occupied flag has been unset
				return;
			}
			// else we hit a PC and will pass right through her
		}
		pos = pos.in(dir);
	}
	// Travelled all the way without hitting anything.
	if(++travelled > ZAP_TRAVEL_DIST)
		makevoid();
	else
	{
		// Update for drawing:
		Game::curmap->mod_tile(pos)->flags |= TF_OCCUPIED;
		event_set.insert(pos);
		symbol = zappath_sym[dir];
	}
}

bool Zap::hit_tree()
{
	/* Chance to hit trees depends on distance travelled and whether has
	 * bounced: */
	char chance;
	if(bounces > 0)
		chance = 70;
	else
	{
		chance = 10;
		if(travelled > 2)
			chance += (travelled - 2)*20;
	}
	/* Chance when hasn't bounced yet is 10% when travelled <= 2 tiles, 70% when
	 * has travelled >= 5 tiles, and linearly in between. */
	return random()%100 < chance;
}


bool Zap::bounce(const e_Dir nd)
{
	if(++bounces == MAX_ZAP_BOUNCES)
	{
		makevoid();
		// This is called only via missile_coll, which is called only via this->update(),
		// occupied flag has been unset.
		return true;
	}
	travelled = 0;
	dir = nd;
	return false;
}

#endif // not maptest build

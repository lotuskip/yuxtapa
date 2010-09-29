// Please see LICENSE file.
#include "viewpoint.h"
#include "actives.h"
#include "declares.h"
#include "../common/los_lookup.h"
#include <cstring>
#include <cstdlib>
#ifdef DEBUG
#include <iostream>
#endif

namespace
{
using namespace std;

const char IN_LOS = 0x01;
const char IS_LIT = 0x02;

// Note that here 0:green, 1:purple, 2: brown, 3:gray, 4:blue, 5:red, 6:yellow
const char fg_map[C_BG_HEAL - BASE_COLOURS] = {
	0, //C_TREE_DIM
	0, //C_GRASS_DIM
	2, // C_ROAD_DIM
	3, // C_WALL_DIM
	2, //C_DOOR_DIM
	3, //C_FLOOR_DIM
	4, // C_WATER_DIM
	0, //C_TREE
	0, //C_GRASS
	2, // C_ROAD
	3, // C_WALL
	2, //C_DOOR
	3, //C_FLOOR
	4, // C_WATER
	0, // C_GREEN_PC
	1, // C_PURPLE_PC
	2, // C_BROWN_PC
	4, // C_WATER_TRAP
	6, // C_LIGHT_TRAP
	0, // C_TELE_TRAP
	3, // C_BOOBY_TRAP
	5, // C_BASE_FIREB_TRAP
	3, // C_NEUT_FLAG
	4, // C_PORTAL_IN
	3, // C_PORTAL_OUT
	2, // C_ARROW
	0, // C_TREE_LIT
	0, // C_GRASS_LIT
	2, // C_ROAD_LIT
	3, // C_WALL_LIT
	2, // C_DOOR_LIT
	3, // C_FLOOR_LIT
	4, // C_WATER_LIT
	0, // C_GREEN_PC_LIT
	1, // C_PURPLE_PC_LIT
	2, // C_BROWN_PC_LIT
	4, // C_WATER_TRAP_LIT
	6, // C_LIGHT_TRAP_LIT
	0, // C_TELE_TRAP_LIT
	3, // C_BOOBY_TRAP_LIT
	5, //C_BASE_FIREB_TRAP_LIT
	3, //C_NEUT_FLAG_LIT
	4, // C_PORTAL_IN_LIT
	3, // C_PORTAL_OUT_LIT
	2, // C_ARROW_LIT
	1, // C_MM1
	1, // C_MM2
	6, // C_TORCH
	6 // C_ZAP
};

const unsigned char VOICE_COLOUR = C_NEUT_FLAG_LIT;
const unsigned char sound_col[] = {
	C_WATER_LIT, // splash
	C_DOOR_LIT, // creak
	C_FIREB_TRAP_LIT, // boom
	C_ZAP, // zap
	C_TELE_TRAP_LIT, // whoosh
	C_WALL_LIT // rumble
};

} // end local namesapce

namespace Game { extern Map *curmap; }

//statics:
char ViewPoint::loslittbl[VIEWSIZE][VIEWSIZE];


ViewPoint::ViewPoint() : LOSrad(SPEC_LOS_RAD), blinded(0)
{
	newmap();
}


void ViewPoint::newmap()
{
	// Resize:
	Game::curmap->make_right_size(ownview);
	// Fill empty; the tiles will be updated with the actual map data as they
	// become visible. The only thing we need from the actual map already at
	// this point are the lit statuses:
	Coords c;
	for(c.y = ownview.size()-1; c.y >= 0; --c.y)
	{
		for(c.x = ownview.size()-1; c.x >= 0; --c.x)
		{
			ownview[c.y][c.x] = EMPTY_TILE;
			if(Game::curmap->get_tile(c).flags & TF_LIT)
				ownview[c.y][c.x].flags |= TF_LIT;
		}
	}

	pos.x = pos.y = ownview.size()/2;
	poschanged = true;
}


short ViewPoint::render(char *target, vector<string> &shouts)
{
	// If the viewpoint is blinded, just render an empty view:
	if(blinded)
	{
		for(int i = 0; i < VIEWSIZE*VIEWSIZE; ++i)
		{
			*(target++) = EMPTY_TILE.cpair;
			*(target++) = EMPTY_TILE.symbol;
		}
		*(target++) = 0; // number of titles
		return VIEWSIZE*VIEWSIZE*2+1;
	}

	char x, y, line, ind;
	Tile *tp;
	Coords c;
	// clear the part of the viewtable that is not constant:
	for(y = 1; y < VIEWSIZE-1; ++y)
	{
		for(x = 1; x < VIEWSIZE-1; ++x)
			loslittbl[x][y] = 0;
	}
	// determine which tiles are lit and which are in LOS (see los_lookup.h)
	for(line = 0; line < LOSrad*8; ++line)
	{
		for(ind = 0; ind < 2*LOSrad; ind += 2)
		{
			// lookup next point (NOTE: x,y point directly to loslittbl!!)
			x = loslookup[LOSrad-2][line*2*LOSrad+ind] + VIEWSIZE/2;
			y = loslookup[LOSrad-2][line*2*LOSrad+ind+1] + VIEWSIZE/2;
			c.x = pos.x + x - VIEWSIZE/2;
			c.y = pos.y + y - VIEWSIZE/2;
			// This might put us outside of the map, in which case the next point in
			// this line will *also* be outside of the map; check:
			if(Game::curmap->point_out_of_map(c))
				break;
			// else safe to fetch the tile:
			tp = &(ownview[c.y][c.x]);
			if(!loslittbl[x][y]) // not already considered
			{
				/* Determine lit status. Note that if the static lit status
				 * of the tile has changed (by a wall being dug out), the
				 * view won't notice this until the tile is within the
				 * "visible-even-if-not-lit" radius. This could be a bug,
				 * but we shall call it a feature. */
				if(tp->flags & TF_LIT // statically lit
					|| is_dynlit(c))
				{
					loslittbl[x][y] = IS_LIT|IN_LOS;
					// update our view of this tile:
					*tp = Game::curmap->get_tile(c);
				}
				else if(ind <= 2*(LOSrad+5)/3) // not lit, still in LOS if in smaller radius
				{
					loslittbl[x][y] = IN_LOS;
					// upd our view of this tile:
					*tp = Game::curmap->get_tile(c);
				}
			}
			// see if need to continue on this ray:
			if(!(tp->flags & TF_SEETHRU))
				break; // not see-through, next line
		}
	}
	// The center point of the view needs to be checked separately.
	// It is always in LOS!
	loslittbl[VIEWSIZE/2][VIEWSIZE/2] = IN_LOS;
	*(tp = &(ownview[pos.y][pos.x])) = Game::curmap->get_tile(pos);
	if(tp->flags & TF_LIT // statically lit
			|| is_dynlit(pos))
		loslittbl[VIEWSIZE/2][VIEWSIZE/2] |= IS_LIT;


	// The actual "rendering":
	unsigned char cp;
	list<string> titles;
	list<Coords> titlecoords;
	for(y = 0; y < VIEWSIZE; ++y)
	{
		for(x = 0; x < VIEWSIZE; ++x)
		{
			c.x = pos.x + x - VIEWSIZE/2;
			c.y = pos.y + y - VIEWSIZE/2;
			if(Game::curmap->point_out_of_map(c))
			{
				// for out of map points, just print emptyness:
				*(target++) = EMPTY_TILE.cpair;
				*(target++) = EMPTY_TILE.symbol;
			}
			else // point is in map
			{
				tp = Game::curmap->mod_tile(c);
				// A sound overruns anything else, so check for it first:
				if(tp->flags & TF_SOUND)
				{
					map<Coords, string>::const_iterator vit = voices.find(c);
					if(vit != voices.end()) // Voice?
					{
						*(target++) = VOICE_COLOUR;
						shouts.push_back("!: " + vit->second);
					}
					else // a regular sound!
						*(target++) = sound_col[reg_sounds[c]];
					*(target++) = '!';
				}
				// If tile is not in LOS (Note that in this table it cannot be
				// "lit but not in LOS" -- if you don't see it, you can't see
				// that it's lit!)
				else if(!(loslittbl[x][y]))
				{
					if(!(tp->flags & TF_OCCUPIED)
						|| !render_pc_indir(c, target))
					{
						// No sound or PC, take the raw symbol and give dim
						// colour. Note that this symbol is taken from ownview,
						// not directly from the map!
						if(ownview[c.y][c.x].cpair == C_UNKNOWN) // can't be dimmed!
							*(target++) = C_UNKNOWN;
						else
							*(target++) = ownview[c.y][c.x].cpair - NUM_DIM_COLS;
						*(target++) = ownview[c.y][c.x].symbol;
					}
				}
				// else (tile in LOS), if tile is occupied must see who's there
				else if((tp->flags & TF_OCCUPIED) &&
					render_occent_at(c, target, loslittbl[x][y] & IS_LIT))
					target += 2;
				// if render_occent returns false, it must be a PC:
				else if((tp->flags & TF_OCCUPIED) &&
					render_pc_at(c, target, loslittbl[x][y] & IS_LIT, titles))
				{
					target += 2;
					titlecoords.push_back(Coords(x,y));
				}
				// else check if there is a nonoccupying entity
				else if(tp->flags & TF_NOCCENT)
				{
					render_noccent_at(c, target, loslittbl[x][y] & IS_LIT);
					target += 2;
				}
				else if(tp->flags & TF_TRAP && owner->team != T_SPEC // specs don't see traps
					&& render_trap_at(c, target, loslittbl[x][y] & IS_LIT))
					target += 2;
				else // tile in LOS, totally empty or unseen trap/PC
				{
					cp = tp->cpair;
					if(loslittbl[x][y] & IS_LIT)
						cp += NUM_NORM_COLS;
					*(target++) = cp;
					*(target++) = tp->symbol;
				}

				// printed something; now must check for an indicator:
				if(tp->flags & TF_ACTION && loslittbl[x][y])
					*(target-2) = C_GREEN_ON_HEAL
						+ (axn_indicators[c]%NUM_ACTIONS)*7
						+ fg_map[*(target-2)-BASE_COLOURS];
			} // point is in map
		} // loop x
} // loop y
	poschanged = false;

	// Add titles in the form [num of titles (char)][so many titles: [x][y][C-string]]
	*(target++) = titles.size();
	short sizeadd = 1;
	while(!titles.empty())
	{
		*(target++) = titlecoords.back().x;
		*(target++) = titlecoords.back().y;
		strcpy(target, titles.back().c_str());
		target += titles.back().size()+1;
		sizeadd += 3 + titles.back().size();
		titlecoords.pop_back();
		titles.pop_back();
	}
	return VIEWSIZE*VIEWSIZE*2+sizeadd;
}

void ViewPoint::move(const e_Dir d) { set_pos(pos.in(d)); }

void ViewPoint::set_pos(const Coords &newpos)
{
	if(!Game::curmap->point_out_of_map(newpos))
	{
		pos = newpos;
		poschanged = true;
	}
}

void ViewPoint::blind()
{
	blinded = 4 + random()%5; // 4-8 turns
	poschanged = true;
}

void ViewPoint::reduce_blind()
{
	if(blinded && !(--blinded))
		poschanged = true; // to force a re-render
}

void ViewPoint::add_watcher(const std::list<Player>::iterator i)
{
	followers.push_back(i);
	poschanged = true; /* Force a render for the new watcher even if nothing
		has changed. */
}

void ViewPoint::drop_watcher(const unsigned short ID)
{
	for(vector< list<Player>::iterator >::iterator it = followers.begin();
		it != followers.end(); ++it)
	{
		if((*it)->stats_i->ID == ID)
		{
			followers.erase(it);
			break;
		}
	}
}


void ViewPoint::move_watchers()
{
	vector< list<Player>::iterator >::iterator it = followers.begin();
	while(it != followers.end())
	{
		if(*it != owner)
		{
			((*it)->viewn_vp = (*it)->own_vp)->add_watcher((*it));
			it = followers.erase(it);
		}
		else
			++it;
	}
}


bool ViewPoint::render_pc_at(const Coords &c, char *target, const bool lit,
	list<string> &titles) const
{
	string tstr;
	for(list<PCEnt>::iterator it = PCs.begin(); it != PCs.end(); ++it)
	{
		if(it->getpos() == c && it->visible_to_team(owner->team)
			&& !it->isvoid())
		{
			// Drawing:
			it->draw(target, lit);
			if(it->is_disguised())
			{
				// disguised: brown for teammates & specs; for enemies their colour
				if(it->get_owner()->team == owner->team || owner->team == T_SPEC)
					*target = C_BROWN_PC;
				else
					*target = team_colour[owner->team -1];
				if(lit)
					*target += NUM_NORM_COLS;
			}

			// Title:
			if(owner->team == T_SPEC || owner->team == it->get_owner()->team)
				tstr = it->get_owner()->nick + '|';
			else
				tstr.clear();
			tstr += it->get_owner()->cl_props.abbr;
			titles.push_back(tstr);

			return true;
		}
	}
	return false;
}

// Draw a PC outside of LOS (one seen by a teammate scout)
bool ViewPoint::render_pc_indir(const Coords &c, char *target) const
{
	list<PCEnt>::iterator it = any_pc_at(c);
	if(it != PCs.end() && it->is_seen_by_team(owner->team) && !it->isvoid())
	{
		it->draw(target, false);
		return true;
	}
	return false;
}

bool ViewPoint::render_trap_at(const Coords &c, char *target, const bool lit) const
{
	list<Trap>::iterator tr_it = any_trap_at(c);
	if(tr_it->is_seen_by(owner->team))
	{
		tr_it->draw(target, lit);
		return true;
	}
	return false;
}

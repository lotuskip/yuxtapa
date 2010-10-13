// Please see LICENSE file.
#ifndef VIEWPOINT_H
#define VIEWPOINT_H

#include "../common/coords.h"
#include "map.h"
#include "players.h"
#include "../common/constants.h"

const char SPEC_LOS_RAD = 10;

class ViewPoint
{
public:
	ViewPoint();
	~ViewPoint() {}

	void set_owner(const std::list<Player>::const_iterator i)
		{ owner = i; }
	void add_watcher(const std::list<Player>::iterator i);
	void drop_watcher(const unsigned short ID);
	void move_watchers(); // set all watchers to watch their own vp
	bool has_watchers() const { return !followers.empty(); }
	std::vector< std::list<Player>::iterator >::const_iterator foll_beg()
		const { return followers.begin(); }
	std::vector< std::list<Player>::iterator >::const_iterator foll_end()
		const { return followers.end(); }
	std::list<Player>::const_iterator get_owner() const { return owner; }

	void newmap(); // notify that map (and possibly its size) has changed
	short render(char *target, std::vector<std::string> &shouts);
	// render returns amount of data written
	bool pos_changed() const { return poschanged; }

	void move(const e_Dir d);
	void set_pos(const Coords &newpos);
	Coords get_pos() const { return pos; }

	void blind();
	void reduce_blind();
	bool is_blind() const { return blinded; }
	void set_losr(const char nr) { LOSrad = nr; }

private:
	char LOSrad; // base radius for this view
	Coords pos; // center of this view
	bool poschanged; // has changed position since last render?
	std::vector< std::vector<Tile> > ownview; /* This is basically
	(an often outdated) copy of the map (Map::data). Every viewpoint
	needs its own copy, since player's should not be aware of e.g.
	walls dug outside of their LOS until they actually see them. */
	char blinded;

	std::list<Player>::const_iterator owner;
	std::vector< std::list<Player>::iterator > followers;

	// This table is shared by all viewpoins and its content is
	// basically rubbish outside of the render()-routine.
	static char loslittbl[VIEWSIZE][VIEWSIZE];

	bool render_pc_at(const Coords &c, char *target, const bool lit,
		std::list<std::string> &titles) const;
	bool render_pc_indir(const Coords &c, char *target) const;
	bool render_trap_at(const Coords &c, char *target, const bool lit) const;
};

#endif

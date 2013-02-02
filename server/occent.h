// Please see LICENSE file.
#ifndef OCCENT_H
#define OCCENT_H

#include "ent.h"
#include "../common/classes_common.h"
#include "players.h"

// an Occupying entity; only one such can be at given coordinates
class OccEnt : public Ent
{
public:
	OccEnt(const Coords &c, const char sym, const char cp);
	virtual ~OccEnt() {}
	virtual void update() {}
};


// An Occupying entity that is owned by a player; pure abstract class.
class OwnedEnt : public OccEnt
{
public:
	OwnedEnt(const Coords &c, const char sym, const char cp,
		const std::list<Player>::iterator o);	
	virtual ~OwnedEnt() {}
	virtual void update() = 0;
	virtual bool hit_tree() { return false; }

	std::list<Player>::iterator get_owner() const { return owner; }
	void set_owner(const std::list<Player>::iterator pp) { owner = pp; }

protected:
	std::list<Player>::iterator owner;
};


class PCEnt : public OwnedEnt
{
public:
	PCEnt(const Coords &c, const std::list<Player>::iterator o);
	~PCEnt() {}
	void update();

	bool visible_to_team(const e_Team t) const
		{ return invis_to_team != t; }
	bool is_seen_by_team(const e_Team t) const
		{ return seen_by_team == t; }
	void set_seen_by_team(const e_Team t) { seen_by_team = t; }
	void set_invis_to_team(const e_Team t) { invis_to_team = t; }

	bool torch_is_lit() const { return torch_lit; }
	void toggle_torch();

	bool is_disguised() const { return disguised; }
	void set_disguised(const bool d) { disguised = d; }

private:
	// "Invisible to team" indicates a PC is hiding (assassin/trapper).
	// This overrules "Seen by team" which indicates that the PC has
	// been spotted by an enemy scout.
	e_Team invis_to_team, seen_by_team;
	bool torch_lit;
	bool disguised;
};


class Arrow : public OwnedEnt
{
public:
	Arrow(Coords t, const std::list<Player>::iterator o);
	~Arrow() {}
	void update();
	bool hit_tree();

private:
	std::list<Coords> path;
	char init_path_len;
};


class MM : public OwnedEnt
{
public:
	MM(const std::list<Player>::iterator o, const e_Dir dir);
	~MM() {}
	void update();
	bool hit_tree();

private:
	e_Dir lmd; // last move dir
	char turns_moved; // only informative for the two first turns
};


class Zap : public OwnedEnt
{
public:
	Zap(const std::list<Player>::iterator o, const e_Dir d);
	~Zap() {}
	void update();
	bool hit_tree();

	bool bounce(const e_Dir nd); // returns true if should die out
	e_Dir get_dir() const { return dir; }

private:
	e_Dir dir;
	char bounces;
	char travelled;
};
#endif

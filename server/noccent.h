// Please see LICENSE file.
#ifndef NOCCENT_H
#define NOCCENT_H

#include "ent.h"
#include "players.h"

// Non-Occupying Entity; several of these can be at the same coordinates.
class NOccEnt : public Ent
{
public:
	NOccEnt(const Coords &c, const char sym, const char cp, const unsigned char m);
	virtual ~NOccEnt() {}
	void update();
	
	void set_misc(const unsigned char nmisc) { misc = nmisc; }
	unsigned char get_m() const { return misc; }

protected:
	unsigned char misc;
	// Meaning of 'misc' depends on entity type:
	// for corpses, it's the amount of turns left before destruction;
	// for traps, it's the trap type (see enum below);
	// for flags, it's the team (same as e_Team, but cannot be T_SPEC);
	// for others, no meaning
};

// trap types
enum { TRAP_WATER=0, TRAP_LIGHT, TRAP_TELE, TRAP_BOOBY, TRAP_FIREB, MAX_TRAP_TYPE };

class Trap : public NOccEnt
{
public:
	Trap(const Coords &c, const unsigned char m,
		const std::list<Player>::iterator o);
	~Trap() {}

	std::list<Player>::iterator get_owner() const { return owner; }
	void set_seen_by(const unsigned char t);
	bool is_seen_by(const unsigned char t) const;

private:
	unsigned char flags;
	std::list<Player>::iterator owner;

};

#endif

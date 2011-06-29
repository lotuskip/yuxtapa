// Please see LICENSE file.
#ifndef ACTIVES_H
#define ACTIVES_H

#include "noccent.h"
#include "occent.h"
#include <list>
#include <map>
#include <set>

enum e_Noccent { NOE_CORPSE=0, NOE_BLOCK_SOURCE, NOE_PORTAL_ENTRY,
	NOE_PORTAL_EXIT, NOE_PORTAL_2WAY, NOE_FLAG, NOE_TORCH, MAX_NOE };

const char TORCH_LIGHT_RADIUS = 8;

// All occupying & non-occupying entities:
extern std::list<NOccEnt> noccents[MAX_NOE];
extern std::list<Trap> traps;
extern std::list<OccEnt> boulders;
extern std::list<Arrow> arrows;
extern std::list<MM> MMs;
extern std::list<Zap> zaps;
extern std::list<PCEnt> PCs;

// Flags owned by teams (green & purple)
extern std::list<std::list<NOccEnt>::iterator> team_flags[2];

// Collection of action indicators:
enum e_Action { A_HEAL=0, A_TRAP, A_POISON, A_DISGUISE, A_HIT, A_MISS,
	NUM_ACTIONS };
extern std::map<Coords, unsigned char> axn_indicators;
extern std::list<Coords> fball_centers; // for fireball-lighting

void add_action_ind(const Coords &c, const e_Action a);
void clear_action_inds();

// Sound-related (e_Sound is defined in common/col_codes.h)
extern std::map<Coords, e_Sound> reg_sounds; // regular sounds
extern std::map<Coords, std::string> voices; // voices have "text content"
void add_sound(const Coords &c, const e_Sound s);
void add_voice(const Coords &c, const std::string &s);
void clear_sounds();

// To know when we need to re-render views, we collect the coordinates
// around which something has changed each turn.
extern std::set<Coords> event_set;
bool events_around(const Coords &pos); // any events close to pos?


void do_placement();
void update_static_light(const Coords &c);

bool render_occent_at(const Coords &c, char *target, const bool lit);
void render_noccent_at(const Coords &c, char *target, const bool lit);

bool is_dynlit(const Coords &c);

// this returns the PC at 'c' or PCs.end() if no one there
std::list<PCEnt>::iterator any_pc_at(const Coords &c);
// similar functions for other entities:
std::list<NOccEnt>::iterator any_noccent_at(const Coords &c,
	const e_Noccent type);
std::list<Trap>::iterator any_trap_at(const Coords &c);
std::list<OccEnt>::iterator any_boulder_at(const Coords &c);
OwnedEnt* any_missile_at(const Coords &c);

// Steal-objective related:
extern std::list<Player>::const_iterator pl_with_item;
extern bool item_moved;
extern NOccEnt the_item;
//pl_with_item == cur_players.end() <=> item lying somewhere.

#endif

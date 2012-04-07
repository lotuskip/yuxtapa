//Please see LICENSE file.
#ifndef CLASSES_COMMON_H
#define CLASSES_COMMON_H

enum e_Class { C_ARCHER=0, C_ASSASSIN, C_COMBAT_MAGE, C_MINDCRAFTER, C_SCOUT,
	C_FIGHTER, C_MINER, C_HEALER, C_WIZARD, C_TRAPPER, C_PLANEWALKER, NO_CLASS };
// NO_CLASS functions as the number of the classes and denotes being a spectator

enum e_Team { T_GREEN=0, T_PURPLE, T_SPEC, 
	T_NO_TEAM }; /* no one's team variable ever has this value, but
it is used to indicate that "no team" sees an entity (as opposed to
"green/purple team sees") */

const e_Team opp_team[] = { T_PURPLE, T_GREEN, T_SPEC };

// The server needs all of these, the client just needs some. But it's easier
// to have them all in the same common structure.
struct ClassProperties
{
	char abbr[3];
	char hp, tohit, magicres, dam_die, dam_add, dv, pv, losr;
	bool torch, can_push;
};

const ClassProperties classes[] =
{//     hp  2hit res  dX+Y  dv pv los  torch   push
{ "Ar",  7,  +3,  0,  4, 1, 18, 1,  9,  true, false },
{ "As",  6, +10, 10,  4, 2, 19, 0,  7, false, false },
{ "Cm",  8,  +2, 50,  8, 1, 14, 2,  7,  true, false },
{ "Mc",  6,  +5, 40, 10, 0, 15, 1,  6, false,  true },
{ "Sc",  4,  +4, 10,  6, 1, 14, 0, 10,  true, false },
{ "Fi", 10,  +8,  0,  8, 2, 17, 5,  5,  true,  true },
{ "Mi", 12,  +6,  0, 10, 1, 13, 3,  5, false,  true },
{ "He",  6,  -1, 35,  6, 1, 12, 0,  5,  true, false },
{ "Wi",  4,  +0, 70,  6, 0, 11, 0,  7,  true, false },
{ "Tr",  8,  +3,  0,  8, 0, 16, 1,  6, false,  true },
{ "Pw",  5,  +7, 60,  6, 2, 14, 2,  6,  true, false }
};

#endif

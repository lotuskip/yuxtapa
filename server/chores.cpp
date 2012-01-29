// Please see LICENSE file.
#ifndef MAPTEST
#include "chores.h"
#include "actives.h"
#include "network.h"
#include "game.h"
#include "viewpoint.h"
#include "spiral.h"
#include "../common/los_lookup.h"
#include "../common/util.h"
#include <cstdlib>
#include <algorithm>
#ifdef DEBUG
#include <iostream>
#include "log.h"
#endif

namespace Game { extern Map *curmap; }
extern e_GameMode gamemode;
extern unsigned short tdm_kills[2];
extern e_Dir obj_sector; // defined in game.cpp

namespace
{
using namespace std;

const string teammate_str = "TEAMMATE ";

const string spell_kill_msg[2][3] = {
/* mms: */ { " could not outrun ", "her own", "magic missile." },
/* zaps */ { " was zapped by ", "herself", "." }
};

const char IDLE_TURNS_TO_AUTOSWAP = 8; /* NOTE: this should be larger than
	the amount of turns it takes to complete any of the "chores" (trap, dig,
	disguise) */
const char MAX_LIMITER = 10;
const char MAX_BLK_DIST = 4;
const char SCARED_TO_DEATH_DIST = 3;
const char CORPSE_DECAY_TURNS = 60; // 15 seconds on normal settings
const char DIG_TURNS = 5;
const char TRAP_TURNS = 6;
const char DISGUISE_TURNS = 7;
const char BASE_TURNS_TO_DETECT_TRAPS = 10;
const char DETECT_TRAPS_RAD = 3;
const char SNEAK_RADIUS = 3;
const char CHANCE_RUST = 65;
const char RUST_MOD = 2;
const char CHANCE_ARROW_TRIG_TRAP = 60;


// Calls update_static_light for all those torches that are close enough
// to c.
void update_lights_around(const Coords &c)
{
	for(list<NOccEnt>::const_iterator it = noccents[NOE_TORCH].begin();
		it != noccents[NOE_TORCH].end(); ++it)
	{
		if(c.dist_walk(it->getpos()) <= TORCH_LIGHT_RADIUS)
			update_static_light(it->getpos());
	}
}

// returns 0: can dig, 1: can't dig, -1: illusory wall!
char cant_dig_in(const Coords &c, const e_Dir d)
{
	// Mining requires one of: (A) there is a wall/window/door, (B) there is a blocked
	// boulder, (C) there is a boulder source.
	Tile* tp = Game::curmap->mod_tile(c);
	if(tp->symbol == '#' && tp->flags & TF_WALKTHRU)
		return -1;
	if(tp->symbol == '#' || tp->symbol == '|' || tp->symbol == '-' || tp->symbol == '+'
		|| (tp->flags & TF_NOCCENT && any_noccent_at(c, NOE_BLOCK_SOURCE)
			!= noccents[NOE_BLOCK_SOURCE].end()))
		return 0;
	// that was (A) and (C); (B) is more complicated:
	if(tp->flags & TF_OCCUPIED && any_boulder_at(c) != boulders.end())
	{
		Coords pushtarpos = c.in(d);
		Tile *pushtar = Game::curmap->mod_tile(pushtarpos);
		if((tp->flags & TF_SLOWWALK) || (pushtar->flags & TF_OCCUPIED)
			|| !(pushtar->flags & TF_WALKTHRU) || pushtar->symbol == '\\'
			|| ((pushtar->flags & TF_NOCCENT)
			&& any_noccent_at(pushtarpos, NOE_FLAG) != noccents[NOE_FLAG].end()))
			return 0; // cannot push; hence can dig
	}
	return 1;
}

void capt_flag(const list<NOccEnt>::iterator fit, const e_Team t)
{
	/* A special situation in which capturing cannot be allowed: game mode is
	 * steal, and green team has secured the treasure: */
	if(gamemode == GM_STEAL && t == T_PURPLE && team_flags[t].empty())
		return;
	// Otherwise okay (various checks already made prior to calling this):
	fit->set_col(team_colour[t]);
	fit->set_misc(t);
	team_flags[t].push_back(fit);
	string msg = str_team[t] + " team captured the "
		+ long_sector_name[Game::curmap->coords_in_sector(fit->getpos())]
		+ " flag!";
	Network::construct_msg(msg, team_colour[t]);
	Network::broadcast();
	// In dominion&conquest this always changes objective status:
	if(gamemode == GM_DOM || gamemode == GM_CONQ)
		Game::send_team_upds(cur_players.end());
	Game::send_flag_upds(cur_players.end());
}


void record_kill(const list<Player>::iterator pit, const char method)
{
	++(pit->stats_i->kills[method]);
	if(gamemode == GM_TDM)
	{
		int cmp = int(tdm_kills[T_GREEN]) - tdm_kills[T_PURPLE];
		++tdm_kills[pit->team];
		// First cmp = A-B, then either A or B is incremented by one, and
		// we want to know whether their relative order has changed. This
		// test suffices:
		if((int(tdm_kills[T_GREEN]) - tdm_kills[T_PURPLE])*cmp == 0)
			Game::send_team_upds(cur_players.end());
	}
}


bool test_hit(const list<Player>::iterator def, const char tohit,
	char numdies, const char ddie, const char dadd)
{
	Coords loc = def->own_pc->getpos();
	char P = def->cl_props.dv - tohit;
	char R = random()%20;
	if(R >= P || R == 19) // hit
	{
		char dam = dadd;
		while(numdies--)
			dam += 1 + random()%ddie;
		if(R != 19 && R/P < 2) // not critical hit; apply PV
			dam -= def->cl_props.pv;
		def->cl_props.hp -= max(0, int(dam));
		def->needs_state_upd = true;
		add_action_ind(loc, A_HIT);
		return true;
	}
	add_action_ind(loc, A_MISS);
	return false;
}


void missile_hit_PC(const list<Player>::iterator pit, OwnedEnt* mis,
	const bool should_void_zap)
{
	list<Player>::iterator shooter = mis->get_owner();
	if(dynamic_cast<Arrow*>(mis)) // it's an arrow
	{
		if(pit->team != shooter->team) // shooting teammates don't count as hit
			shooter->stats_i->arch_hits++;

		if(test_hit(pit, 7, 1, 6, 0)) // arrows: 1d6+0 damage, +7 to hit
		{
			if(pit->cl_props.hp <= 0) // (check if died)
			{
				string msg = " was shot by ";
				if(pit->team == shooter->team)
				{
					msg += teammate_str;
					shooter->stats_i->tks++;
				}
				else // shot enemy, count kill
					record_kill(shooter, WTK_ARROW);
				player_death(pit, msg + shooter->nick + '.', true);
			}
			else
				pit->needs_state_upd = true;
		}
		mis->makevoid(); // occupied flag is unset in kill_player if necessary
	}
	else // a mm or a zap
	{
		bool is_zap = dynamic_cast<Zap*>(mis);
		if(is_zap && shooter->team != pit->team)
			shooter->stats_i->cm_hits++;
		if(pit->nomagicres())
		{
			add_action_ind(pit->own_pc->getpos(), A_HIT);

			char dam = 1 + random()%5; // 1d5
			if(is_zap)
				dam += 1 + random()%5; // 2d5 for zaps
			pit->cl_props.hp -= dam;
			if(pit->cl_props.hp <= 0) // died
			{
				string msg = spell_kill_msg[is_zap][0];
				if(pit == shooter)
					msg += spell_kill_msg[is_zap][1];
				else
				{
					if(pit->team == shooter->team)
					{
						msg += teammate_str;
						shooter->stats_i->tks++;
					}
					else
						record_kill(shooter, is_zap ? WTK_ZAP : WTK_MM);
					msg += shooter->nick;
					if(!is_zap)
						msg += "\'s ";
				}
				msg += spell_kill_msg[is_zap][2];
				player_death(pit, msg, true);
			}
			else
				pit->needs_state_upd = true;
		} // no magic resistance
		else
			add_action_ind(pit->own_pc->getpos(), A_MISS);
		// mms voided always, zaps only if we are allowed to do so here:
		if(should_void_zap || !is_zap)
			mis->makevoid(); // occupied flag is unset in kill_player if necessary
	} // mm or zap
}


void heal_PC(const list<Player>::iterator pit)
{
	if(pit->cl_props.hp < classes[pit->cl].hp)
	{
		pit->cl_props.hp++;
		pit->stats_i->healing_recvd++;
		pit->poisoner = cur_players.end();
		pit->needs_state_upd = true;
		add_action_ind(pit->own_pc->getpos(), A_HEAL);
	}
}

void conditional_blind(const Coords &c)
{
	list<PCEnt>::iterator pc_it = any_pc_at(c);
	// Must be valid PC, not assassin, and not a mind's eye using planewalker:
	if(pc_it != PCs.end() && pc_it->get_owner()->cl != C_ASSASSIN
		&& (pc_it->get_owner()->cl != C_PLANEWALKER || !pc_it->get_owner()->limiter))
		pc_it->get_owner()->own_vp->blind();
}

void flash_at(const Coords &pos)
{
	// Every non-assassin within LOS is blinded (note that
	// the radius is the same as for trap detection).
	char line, ind;
	Coords c;
	unsigned short f;
	list<PCEnt>::iterator pc_it;
	for(line = 0; line < DETECT_TRAPS_RAD*8; ++line)
	{
		for(ind = 0; ind < 2*DETECT_TRAPS_RAD; ind += 2)
		{
			c.x = loslookup[DETECT_TRAPS_RAD-2][line*2*DETECT_TRAPS_RAD+ind] + pos.x;
			c.y = loslookup[DETECT_TRAPS_RAD-2][line*2*DETECT_TRAPS_RAD+ind+1] + pos.y;
			if((f = Game::curmap->get_tile(c).flags) & TF_OCCUPIED)
				conditional_blind(c);
			else if(!(f & TF_SEETHRU))
				break;
		}
	}
	// At pos itself, too:
	conditional_blind(pos);
	add_sound(pos, S_ZAP);
}


void fireball_trigger(const list<Trap>::iterator tr, const Coords &pos,
	const list<Player>::iterator triggerer)
{
	Coords c;
	list<PCEnt>::iterator pc_it;
	char ch;
	string msg;
	for(c.x = min(pos.x+2, Game::curmap->get_size()-2); c.x >= max(1, pos.x-2); --(c.x))
	{
		for(c.y = min(pos.y+2, Game::curmap->get_size()-2); c.y >= max(1, pos.y-2); --(c.y))
		{
			add_sound(c, S_BOOM);
			if((c == pos /* occupied-flag might not be set for the triggerer yet;
				we check this point unnecessarily when triggering with an arrow. */
				|| (Game::curmap->get_tile(c).flags & TF_OCCUPIED))
				&& (pc_it = any_pc_at(c)) != PCs.end())
			{
				// damage is (3-radius)d6:
				for(ch = 3 - pos.dist_walk(c); ch > 0; --ch)
					pc_it->get_owner()->cl_props.hp -= 1 + random()%6;
				if(pc_it->get_owner()->cl_props.hp <= 0) // died
				{
					if(pc_it->get_owner() == triggerer)
						player_death(triggerer, " fried to a trap.", false);
					else
					{
						msg = " was taken to the flames by ";
						if(pc_it->get_owner()->team == triggerer->team)
							msg += teammate_str; // note: no team kill recorded
						else
							record_kill(pc_it->get_owner(), WTK_FIREB);
						msg += triggerer->nick + '.';
						player_death(pc_it->get_owner(), msg, false);
					}
				}
				else // survived
				{
					pc_it->get_owner()->needs_state_upd = true;
					// light torch if any:
					if(!pc_it->torch_is_lit() && pc_it->get_owner()->torch_left)
						pc_it->toggle_torch();
				}
			}
		}
	}
	fball_centers.push_back(pos);
}


void move_player_to(const list<Player>::iterator p, const Coords &c,
	const bool do_flags); // (This needs to call trigger_trap needs to call this)

bool trigger_trap(const list<Player>::iterator pit, const list<Trap>::iterator tr)
{
	Coords pos = pit->own_pc->getpos();
	switch(tr->get_m())
	{
	case TRAP_WATER:
		// Extinguish torch, make splash sound, possibly rust weapon
		if(pit->own_pc->torch_is_lit())
			pit->own_pc->toggle_torch();
		add_sound(pos, S_SPLASH);
		// Rusting happens only once; to see if it has happened we compare
		// the PCs 2-hit to the 2-hit of that class by default:
		if(pit->cl_props.tohit == classes[pit->cl].tohit
			&& random()%100 < CHANCE_RUST)
		{
			string msg = "Your weapon rusts!";
			Network::construct_msg(msg, C_WATER_TRAP_LIT);
			Network::send_to_player(*pit);
			pit->cl_props.tohit -= RUST_MOD;
			pit->cl_props.dam_add -= RUST_MOD;
			pit->needs_state_upd = true;
		}
		break;
	case TRAP_LIGHT:
		flash_at(pos);
		break;
	case TRAP_TELE:
		if(pit->nomagicres())
		{
			// Get a random tile that's walkable and not occupied:
			Coords tar(1 + random()%(Game::curmap->get_size()-1),
				1 + random()%(Game::curmap->get_size()-1));
			init_nearby(tar);
			unsigned short f = Game::curmap->get_tile(tar).flags;
			while(!(f & TF_WALKTHRU) || (f & TF_OCCUPIED))
				f = Game::curmap->get_tile((tar = next_nearby())).flags;
			event_set.insert(pos); // necessary if 'T'riggering the trap
			move_player_to(pit, tar, true);
			// NOTE: do not clear axn queue like in blinking
			return true; // do not make triggered teleportation traps seen
		}
		else
			add_action_ind(pos, A_MISS);
		break;
	case TRAP_BOOBY:
		if(test_hit(pit, 9, 2, 6, 0) // 2d6+0, +9 tohit
			&& pit->cl_props.hp <= 0)
		{
			string msg = " died to a trap.";
			if(tr->get_owner() != cur_players.end())
			{
				msg = " failed to notice ";
				if(tr->get_owner() == pit)
					msg += "her own";
				else
				{
					if(tr->get_owner()->team == pit->team)
					{
						msg += teammate_str;
						tr->get_owner()->stats_i->tks++;
					}
					else
						record_kill(tr->get_owner(), WTK_BOOBY);
					msg += tr->get_owner()->nick + "\'s";
				}
				msg += " boobytrap.";
			}
			player_death(pit, msg, true);
		}
		add_sound(pos, S_CREAK);
		// Boobytraps are always destroyed:
		traps.erase(tr);
		Game::curmap->mod_tile(pos)->flags &= ~(TF_TRAP);
		return !pit->is_alive(); // cannot set the destroyed trap as seen!
	case TRAP_FIREB:
		fireball_trigger(tr, pos, pit);
		if(!pit->is_alive())
			return true;
		break;
	case TRAP_ACID:
		if(test_hit(pit, 7, 3, 3, 0) // 3d3+0, +7 tohit
			&& pit->cl_props.hp <= 0)
			player_death(pit, " dissolved.", true);
		add_sound(pos, S_SPLASH);
		if(!pit->is_alive())
			return true;
		break;
	//default: anything else is an error!
	}
	// If here, trap should be made known:
	tr->set_seen_by(pit->team);
	return false;
}


void move_player_to(const list<Player>::iterator p, const Coords &c,
	const bool do_flags)
{
	Coords opos = p->own_pc->getpos();
	// must move both the PC entity and the viewpoint:
	p->own_pc->setpos(c);
	p->own_vp->set_pos(c);
	if(p == pl_with_item)
		add_action_ind(c, A_TRAP);
	else // add_action inserts to event_set
		event_set.insert(c);

	// Check if ended up in some special tile: (note that some of
	// these are checked only when *walking* into them, but in
	// this function we don't know the "method of arrival")
	Tile* tile = Game::curmap->mod_tile(c);
	if(tile->flags & TF_KILLS) // chasm
	{
		player_death(p, " fell into oblivion.", false);
		add_voice(c, "Aieeeee...!");	
		Game::curmap->mod_tile(opos)->flags &= ~(TF_OCCUPIED);
		return; // dead, no need to check anything else
	}
	if(tile->flags & TF_TRAP)
	{
		list<Trap>::iterator tr_it = any_trap_at(c);
		// A trap is triggered always if not seen and sometimes if it's dark:
		if(!tr_it->is_seen_by(p->team)
			|| (!(tile->flags & TF_LIT) && random()%100 < 15))
		{
			if(trigger_trap(p, tr_it))
			{
				// if returned true, PC died or teleported
				Game::curmap->mod_tile(opos)->flags &= ~(TF_OCCUPIED);
				return;
			}
		}
	}
	
	// If we made it here, the PC survived the checks.
	if(tile->flags & TF_DROWNS) // water slows down
	{
		p->wait_turns += 2;
		if(p->cl != C_ASSASSIN && p->cl != C_SCOUT)
			add_sound(c, S_SPLASH);
	}
	else if((tile->flags & TF_SLOWWALK) // slow non-trappers/scouts
		&& p->cl != C_TRAPPER && p->cl != C_SCOUT)
		p->wait_turns++;
	
	// The occupied flags don't need to be changed when switching places:
	if(do_flags)
	{
		Game::curmap->mod_tile(opos)->flags &= ~(TF_OCCUPIED);
		Game::curmap->mod_tile(c)->flags |= TF_OCCUPIED;
	}
	
	// Check if moved to another sector:
	e_Dir newsector = Game::curmap->coords_in_sector(c);
	if(p->sector != newsector)
	{
		p->sector = newsector;
		p->needs_state_upd = true;
	}

	// Whenever a PC moves, we check if they spot some hiding enemies.
	// The requirement for this is that this PC is facing directly at a hiding
	// enemy, whose position is lit, at a distance of 2.
	opos = c.in(p->facing).in(p->facing); // reusing opos
	list<PCEnt>::iterator pc_it = any_pc_at(opos);
	if(pc_it != PCs.end() && !pc_it->isvoid() && !pc_it->visible_to_team(p->team)
		&& ((Game::curmap->get_tile(opos).flags & TF_LIT) || is_dynlit(opos)))
		pc_it->set_invis_to_team(T_NO_TEAM); // seen!

	// Also, whenever a PC moves, we check if they are seen by enemy scouts:
	if(p->own_pc->visible_to_team(opp_team[p->team]))
	{
		Coords thc; // "their coords"
		char d = classes[C_SCOUT].losr;
		if(!(Game::curmap->get_tile(c).flags & TF_LIT))
			d = (d+5)/3; // not lit, shorter radius
		for(list<Player>::const_iterator pit = cur_players.begin();
			pit != cur_players.end(); ++pit)
		{
			// Search for opponent scouts who are alive...
			if(pit->team != p->team && pit->cl == C_SCOUT && pit->is_alive()
				&& Game::curmap->LOS_between(c, pit->own_pc->getpos(), d))
			{
				p->own_pc->set_seen_by_team(pit->team);
				return; // done
			}
		} // loop through current players
	}
	// If we end up here, was not seen by enemy scouts:
	p->own_pc->set_seen_by_team(T_NO_TEAM);
}


void swap_places(const list<Player>::iterator pit1, const list<Player>::iterator pit2)
{
	Coords pos1 = pit1->own_pc->getpos();
	Coords pos2 = pit2->own_pc->getpos();
	move_player_to(pit1, pos2, false);
	move_player_to(pit2, pos1, false);
	pit1->wants_to_move_to = pit2->wants_to_move_to = MAX_D;
	// A very rare possibility is that one of the players dies due to a
	// trap; we must check this:
	if(!pit1->is_alive())
		Game::curmap->mod_tile(pos1)->flags &= ~(TF_OCCUPIED);
	if(!pit2->is_alive())
		Game::curmap->mod_tile(pos2)->flags &= ~(TF_OCCUPIED);
}


void portal_jump(const list<Player>::iterator pit, const Coords &tar)
{	
	add_sound(pit->own_pc->getpos(), S_WHOOSH);
	add_sound(tar, S_WHOOSH);
	move_player_to(pit, tar, true);
}


void try_move(const list<Player>::iterator pit, const e_Dir d)
{
	// get target tile and its position:
	Coords opos = pit->own_pc->getpos();
	Coords tarpos = opos.in(d);
	Tile *tar = Game::curmap->mod_tile(tarpos.x, tarpos.y);
	list<Player>::iterator them;
	// unwalkable tiles are unwalkable tiles:
	if(!(tar->flags & TF_WALKTHRU))
	{
		// Check if walking to a closed door; if so, open it
		if(tar->symbol == '+')
		{
			tar->symbol = '\\';
			tar->flags |= TF_WALKTHRU|TF_SEETHRU;
			update_lights_around(tarpos);
			if(random()%2)
				add_sound(tarpos, S_CREAK);
			else // a sound sets an event
				event_set.insert(tarpos);
		}
		return; // do not move
	}
	/*else*/ if(tar->flags & TF_OCCUPIED)
	{
		// Tile is occupied; first check for another PC:
		list<PCEnt>::iterator pc_it = any_pc_at(tarpos);
		if(pc_it != PCs.end())
		{
			them = pc_it->get_owner();
			if(them->team == pit->team) // teammate
			{
				if(them->wants_to_move_to == !d) // That player is willing to swap places
					swap_places(them, pit);
				else // the processing of this move is finished later
					pit->wants_to_move_to = d;
			}
			else // enemy, do melee attack:
			{
				// if a hiding assassin, damage is different:
				char multip = 1 + char(pit->cl == C_ASSASSIN
					&& (!pit->own_pc->visible_to_team(opp_team[pit->team])
					|| them->own_vp->is_blind()));
				if(test_hit(them, pit->cl_props.tohit, multip, pit->cl_props.dam_die,
					multip*(pit->cl_props.dam_add)) // was hit...
					&& them->cl_props.hp <= 0) // ...and died
				{
					if(multip == 2)
					{
						player_death(them, " was backstabbed by " + pit->nick + '.', true);
						record_kill(pit, WTK_BACKSTAB);
					}
					else
					{
						player_death(them, " was killed by " + pit->nick + '.', true);
						record_kill(pit, WTK_MELEE);
					}
				}
				pit->own_pc->set_disguised(false);
			}
			return; // do not move
		}
		// else check all other occents; first blocks:
		list<OccEnt>::iterator bl_it = any_boulder_at(tarpos);
		if(bl_it != boulders.end()) // there's a block
		{
			// First check if PC can push blocks:
			if(!pit->cl_props.can_push)
				return; // do not move
			// Get the tile where we'd push the block:
			Coords pushtarpos = tarpos.in(d);
			Tile *pushtar = Game::curmap->mod_tile(pushtarpos);
			// Cannot push if block is currently standing on nonflat terrain.
			// Cannot push into nonwalkables, occupieds, doorways, flags.
			if((tar->flags & TF_SLOWWALK) || (pushtar->flags & TF_OCCUPIED)
				|| !(pushtar->flags & TF_WALKTHRU) || pushtar->symbol == '\\'
				|| ((pushtar->flags & TF_NOCCENT)
				&& any_noccent_at(pushtarpos, NOE_FLAG) != noccents[NOE_FLAG].end()))
				return; // do not move
			// Pushing to a chasm destroys the block:
			if(pushtar->flags & TF_KILLS)
			{
				boulders.erase(bl_it);
				tar->flags &= ~(TF_OCCUPIED);
			}
			// Pushing to water makes a "bridge":
			else if(pushtar->flags & TF_DROWNS)
			{
				boulders.erase(bl_it);
				tar->flags &= ~(TF_OCCUPIED);
				*pushtar = T_FLOOR;
				add_sound(pushtarpos, S_SPLASH);
			}
			else // otherwise we just push:
			{
				bl_it->setpos(pushtarpos);
				pushtar->flags |= TF_OCCUPIED;
				tar->flags &= ~(TF_OCCUPIED);
			}
			pit->wait_turns++;
			// PC will be moved below, and that will also add to event_set.
		}
		else // no block, must be arrow/zap/mm
		{
			// A nasty hack needed. If the PC should die to the missile, that calls
			// player_death, which generates a corpse to pit->own_pc->getpos(), which
			// is the *wrong* place for it! Hence we move the PC (just the PC, nothing
			// else) already here:
			pit->own_pc->setpos(tarpos);
			missile_hit_PC(pit, any_missile_at(tarpos), true);
			if(pit->cl_props.hp <= 0) // died; don't walk!
			{
				// missile_hit_PC has called kill_player, which has unset the
				// occupied-flag of PC's position (which we just set with our
				// hack), but now we must unset the flag for the originating
				// position, too!
				Game::curmap->mod_tile(opos)->flags &= ~(TF_OCCUPIED);
				return;
			}
			// else survived; let walk; but so that the flags are updated
			// correctly, put the PC back where he's walking from:
			pit->own_pc->setpos(opos);
		}
	} // target tile is occupied

	// Check for various effects of *walking* into various things:
	// (the rest, those that apply regardless of whether walking or not,
	// are checked in move_player_to(...))
	if(tar->flags & TF_NOCCENT)
	{
		list<NOccEnt>::iterator noe_it = any_noccent_at(tarpos, NOE_FLAG);
		if(noe_it != noccents[NOE_FLAG].end()) // there's a flag
		{
			// A neutral flag is always captured:
			if(noe_it->get_m() == T_NO_TEAM)
				capt_flag(noe_it, pit->team);
			// Enemy flag capturing depends on game mode.
			else if(noe_it->get_m() != pit->team)
			{
				// dominion: capture always
				// conquest: green team can capture
				// others: capture if not their last flag
				if(gamemode == GM_DOM
					|| (gamemode == GM_CONQ && pit->team == T_GREEN)
					|| (gamemode != GM_CONQ &&
					team_flags[noe_it->get_m()].size() >= 2))
				{
					team_flags[noe_it->get_m()].erase(find(team_flags[noe_it->get_m()].begin(),
						team_flags[noe_it->get_m()].end(), noe_it));
					capt_flag(noe_it, pit->team);
				}
			}
			// else own flag:
			else if(pit == pl_with_item // is carrying item (implies team is green and gamemode steal)
				&& noe_it == *(team_flags[T_GREEN].begin())) // original flag
			{
				// Green wins. We ensure this by clearing the purple flags list,
				// so actually the map won't end until the next purple spawning:
				team_flags[T_PURPLE].clear();
				string msg = "The green team has secured the treasure!";
				Network::construct_msg(msg, C_PORTAL_IN_LIT);
				Network::broadcast();
				pl_with_item = cur_players.end();
				the_item.setpos(pit->own_pc->getpos());
			}
		}
		else if((noe_it = any_noccent_at(tarpos, NOE_PORTAL_ENTRY))
			!= noccents[NOE_PORTAL_ENTRY].end())
		{
			// Get a random, non-blocked 1-way portal exit:
			char ind = randor0(noccents[NOE_PORTAL_EXIT].size());
			for(noe_it = noccents[NOE_PORTAL_EXIT].begin(); ind > 0; --ind)
				++noe_it;
			list<NOccEnt>::iterator startit = noe_it;
			while(Game::curmap->mod_tile(noe_it->getpos())->flags & TF_OCCUPIED)
			{
				if(++noe_it == noccents[NOE_PORTAL_EXIT].end())
					noe_it = noccents[NOE_PORTAL_EXIT].begin();
				if(noe_it == startit) // went around!
				{
					// Just move the PC onto the portal without jumping
					move_player_to(pit, tarpos, true);
					return;
				}
			}
			portal_jump(pit, noe_it->getpos());
			return;
		}// else
		if((noe_it = any_noccent_at(tarpos, NOE_PORTAL_2WAY))
			!= noccents[NOE_PORTAL_2WAY].end())
		{
			list<NOccEnt>::iterator startit = noe_it;
			do {
				if(++noe_it == noccents[NOE_PORTAL_2WAY].end())
					noe_it = noccents[NOE_PORTAL_2WAY].begin();
				if(noe_it == startit) // went around!
				{
					// Just move the PC onto the portal without jumping
					move_player_to(pit, tarpos, true);
					return;
				}
			}
			while(Game::curmap->mod_tile(noe_it->getpos())->flags & TF_OCCUPIED);
			portal_jump(pit, noe_it->getpos());
			return;
		} // else
		if(gamemode == GM_STEAL && pit->team == T_GREEN
			&& pl_with_item == cur_players.end() && tarpos == the_item.getpos())
		{
			pl_with_item = pit;
			item_moved = true;
			Game::send_team_upds(cur_players.end());
			string msg = "The green team has stolen the treasure!";
			Network::construct_msg(msg, C_PORTAL_IN_LIT);
			Network::broadcast();
		}
	}

	// Hiding checks:
	bool hide = false;
	if(!(tar->flags & TF_LIT)
		&& ((pit->cl == C_ASSASSIN && (tar->flags & TF_BYWALL))
		|| (pit->cl == C_TRAPPER && tar->symbol == 'T'))
		&& !is_dynlit(tarpos))
	{
		// No enemy may see here; note that we use a fixed radius 3,
		// not the LOS radius of the respective enemy!
		for(them = cur_players.begin(); them != cur_players.end(); ++them)
		{
			if(them->team != pit->team && them->is_alive()
				&& !them->own_vp->is_blind()
				&& Game::curmap->LOS_between(them->own_pc->getpos(),
					tarpos, SNEAK_RADIUS))
				break; // seen by an enemy
		}
		if(them == cur_players.end()) // seen by no-one!
			hide = true;
	}
	pit->own_pc->set_invis_to_team(hide ? opp_team[pit->team] : T_NO_TEAM);

	// If we ever arrive here, we are to move the PC:
	move_player_to(pit, tarpos, true);
}


void limiter_upd(const list<Player>::iterator pit)
{
	if(pit->limiter < MAX_LIMITER)
		pit->limiter++;
	pit->wait_turns += 2*pit->limiter;
}

// Used when mining open a tile. If it is a wall tile (not window or door), this should
// be follows by finish_wall_dig([coords]). Otherwise (if window or door), it should be
// followed by event_set.insert(c).
void dig_open(Tile* const tp)
{
	tp->symbol = '.';
	tp->flags |= TF_WALKTHRU|TF_SEETHRU;
}

void finish_wall_dig(const Coords &c)
{
	update_lights_around(c);
	// Need to update BYWALL for neighbours:
	e_Dir dir = D_N;
	Coords cn;
	do {
		cn = c.in(dir);
		Game::curmap->upd_by_wall_flag(cn);
	} while(++dir != D_N);
	event_set.insert(c);
}

} // end local namespace


// This function assumes that pit points to a player who is alive, and
// axn is an action that "makes sense for that player to do"
void process_action(const Axn &axn, const list<Player>::iterator pit)
{
	// Stuff that happens whenever a PC acts:
	pit->acted_this_turn = true;
	pit->wants_to_move_to = MAX_D;

	switch(axn.xncode)
	{
	case XN_CLOSE_DOOR:
	{
		// Find an open door, starting the search from the facing direction:
		e_Dir d = pit->facing;
		Tile *t;
		Coords c;
		do {
			c = pit->own_pc->getpos().in(d);
			t = Game::curmap->mod_tile(c);
			if(t->symbol == '\\' && !(t->flags & TF_OCCUPIED))
			{
				t->symbol = '+';
				t->flags &= ~(TF_WALKTHRU|TF_SEETHRU);
				update_lights_around(c);
				if(random()%2)
					add_sound(c, S_CREAK);
				else // a sound sets an event
					event_set.insert(c);
				pit->facing = d;
				break;
			}
			++d;
		} while(d != pit->facing);
		break;
	}
	case XN_TORCH_HANDLE:
	{
		Coords pos = pit->own_pc->getpos();
		if(pit->own_pc->torch_is_lit())
		{
			pit->own_pc->toggle_torch();
			event_set.insert(pos);
		}
		else if(pit->torch_left) // wants to light it
		{
			// Check the three conditions: wizard, on a static torch, or
			// next to a teammate with a lit torch:
			if(pit->cl == C_WIZARD || any_noccent_at(pos, NOE_TORCH) != noccents[NOE_TORCH].end())
			{
				pit->own_pc->toggle_torch();
				event_set.insert(pos);
			}
			else // Check the teammates:
			{
				for(list<PCEnt>::const_iterator pcit = PCs.begin();
					pcit != PCs.end(); ++pcit)
				{
					if(pcit->getpos().dist_walk(pos) == 1
						&& pcit->torch_is_lit()
						&& pcit->get_owner()->team == pit->team)
					{
						pit->own_pc->toggle_torch();
						event_set.insert(pos);
						break;
					}
				}
			}
		} // trying to light a torch
		break;
	}
	case XN_TRAP_TRIGGER:
		if(Game::curmap->get_tile(pit->own_pc->getpos()).flags & TF_TRAP)
			trigger_trap(pit, any_trap_at(pit->own_pc->getpos()));
		break;
	case XN_SUICIDE:
	{
		// Check for "scared to death" possibility:
		// Go through the PCs looking for any visible enemy <= 3 tiles away
		list<PCEnt>::iterator it = PCs.begin();
		while(it != PCs.end())
		{
			// not void, visible, enemy, close enough:
			if(!it->isvoid() && it->visible_to_team(pit->team)
				&& it->get_owner()->team != pit->team
				&& Game::curmap->LOS_between(it->getpos(), pit->own_pc->getpos(),
					SCARED_TO_DEATH_DIST))
			{
				player_death(pit, " was scared to death by "
					+ it->get_owner()->nick + '.', true);
				record_kill(it->get_owner(), WTK_SCARE);
				break;
			}
			++it;
		}
		if(it != PCs.end()) // was broken
			break;
		// If here, check for being poisoned with just 1 hp:
		if(pit->poisoner != cur_players.end() && pit->cl_props.hp == 1)
			player_poisoned(pit);
		else // a valid suicide
			player_death(pit, " suicided.", true);
		break;
	}
	case XN_MOVE:
		// If player is in water and action queue is nonempty, do not allow swimming
		if(pit->action_queue.empty() ||
			!(Game::curmap->mod_tile(pit->own_pc->getpos())->flags & TF_DROWNS))
			try_move(pit, (pit->facing = e_Dir(axn.var1)));
		break;
	case XN_SHOOT:
	{
		// The coords are already validated in network.cpp upon receiving!
		Coords targetc(axn.var1, axn.var2);
		pit->facing = Coords(0,0).dir_of(targetc);
		pit->stats_i->arch_shots++;
		arrows.push_back(Arrow(targetc, pit));
		break;
	}
	case XN_FLASH:
		if(pit->limiter) // have flashbombs left
		{
			flash_at(pit->own_pc->getpos());
			pit->limiter--;
		}
		break;
	case XN_ZAP:
		limiter_upd(pit);

		pit->facing = e_Dir(axn.var1);
		zaps.push_back(Zap(pit, pit->facing));
		pit->stats_i->cm_shots++;
		break;
	case XN_CIRCLE_ATTACK:
		pit->facing = e_Dir(axn.var1);
		pit->doing_a_chore = 2;
		break;
	case XN_HEAL:
		if(axn.var1 != MAX_D)
		{
			Coords c = pit->own_pc->getpos().in((pit->facing = e_Dir(axn.var1)));
			list<PCEnt>::iterator pc_it = any_pc_at(c);
			if(pc_it != PCs.end())
			{
				list<Player>::iterator nit = pc_it->get_owner();
				if(nit->team == pit->team) // teammate => heal
					heal_PC(nit);
				else // enemy => poison
				{
					nit->poisoner = pit;
					add_action_ind(c, A_POISON);
				}
			}
		}
		else // healing self
			heal_PC(pit);
		break;
	case XN_BLINK:
	{
		limiter_upd(pit);

		// Determine a point to blink to, if any:
		Coords c;
		Coords opos = pit->own_pc->getpos();
		vector<Coords> optimals;
		Coords cg = opos; // "good" coords
		char line, ind;
		for(line = 0; line < MAX_BLK_DIST*8; ++line)
		{
			for(ind = 0; ind < 2*MAX_BLK_DIST; ind += 2)
			{
				c.x = loslookup[MAX_BLK_DIST-2][line*2*MAX_BLK_DIST+ind]
					+ opos.x;
				c.y = loslookup[MAX_BLK_DIST-2][line*2*MAX_BLK_DIST+ind+1]
					+ opos.y;
				// the point must be visible:
				if(!(Game::curmap->get_tile(c).flags & TF_SEETHRU))
					break;
				//else
				cg = c;
			}
			// Note that points outside of the map cannot be reached; the edge
			// wall prevents them from being visible!
			// Endpoint must be walkable and not occupied:
			if((Game::curmap->get_tile(cg).flags & TF_WALKTHRU)
				&& !(Game::curmap->get_tile(cg).flags & TF_OCCUPIED))
			{
				// Trying to get as far as possible:
				if(optimals.empty())
					optimals.push_back(cg); // no previous record
				else if(cg.dist_walk(opos) > optimals.front().dist_walk(opos))
				{
					optimals.clear(); // a new record!
					optimals.push_back(cg);
				}
				else if(cg.dist_walk(opos) == optimals.front().dist_walk(opos))
					optimals.push_back(cg); // repeat old record
			}
		}
		if(optimals.empty())
			break; // cannot blink!
		c = optimals[randor0(optimals.size())];

		// Do the transfer:
		event_set.insert(opos);
		move_player_to(pit, c, true);
		pit->action_queue.clear(); /* Rarely does one plan her actions
			beyond a blink. */
		break;
	}
	case XN_MINE:
	{
		pit->facing = e_Dir(axn.var1);
		Coords c = pit->own_pc->getpos().in(e_Dir(axn.var1));
		char rv;
		if(!(rv = cant_dig_in(c, e_Dir(axn.var1))))
		{
			pit->doing_a_chore = DIG_TURNS;
			add_sound(c, S_RUMBLE);
		}
		else if(rv == -1) // illusory wall
		{
			string msg = "That is no real wall!";
			Network::construct_msg(msg, C_WALL_LIT);
			Network::send_to_player(*pit);
		}
		// else there is nothing to dig
		break;
	}
	case XN_DISGUISE:
	{
		Coords pos = pit->own_pc->getpos();
		if(Game::curmap->get_tile(pos).flags & TF_NOCCENT)
		{
			// check that there's an enemy corpse
			list<NOccEnt>::iterator c_it = any_noccent_at(pos, NOE_CORPSE);
			if(c_it != noccents[NOE_CORPSE].end()
				&& c_it->get_colour() != team_colour[pit->team])
			{
				pit->doing_a_chore = DISGUISE_TURNS;
				add_action_ind(pos, A_DISGUISE);
			}
		}
		break;
	}
	case XN_SET_TRAP:
	{
		// A trap can be set/disarmed where there is no noccent:
		if(!(Game::curmap->get_tile(pit->own_pc->getpos()).flags
			& TF_NOCCENT))
		{
			pit->doing_a_chore = TRAP_TURNS;
			add_action_ind(pit->own_pc->getpos(), A_TRAP);
		}
		break;
	}
	case XN_MM:
	{
		limiter_upd(pit);

		Coords pos;
		e_Dir d;
		unsigned short flgs;
		for(char ch = 0; ch < 4; ++ch)
		{
			d = e_Dir((pit->facing + 2*ch)%MAX_D);
			pos = pit->own_pc->getpos().in(d);
			flgs = Game::curmap->mod_tile(pos)->flags;
			if(flgs & TF_WALKTHRU && !(flgs & TF_OCCUPIED))
			{
				MMs.push_back(MM(pit, d));
				event_set.insert(pos);
			}
			// else cannot conjure a MM there
		}
		break;
	}
	case XN_MINDS_EYE:
		if(!(pit->limiter = !pit->limiter)) // turning mind's eye off
		{
			pit->own_vp->set_pos(pit->own_pc->getpos());
			pit->cl_props.dv = classes[pit->cl].dv;
		}
		else // turning mind's eye on
		{
			pit->cl_props.dv = 11;
			while(pit->own_vp->is_blind())
				pit->own_vp->reduce_blind();
		}
		pit->needs_state_upd = true;
		break;
	}
}


void player_death(const list<Player>::iterator pit, const string &way,
	const bool corpse)
{
	string msg = pit->nick + way;
	Network::construct_msg(msg, DEF_MSG_COL);
	Network::broadcast();
	pit->stats_i->deaths++;
	pit->needs_state_upd = true;
	Coords pos;
	// Item carrier leaves no corpse! Also, if the place is a water square,
	// do not generate a corpse:
	if(corpse && pit != pl_with_item &&
		(Game::curmap->mod_tile((pos = pit->own_pc->getpos()))->flags & TF_DROWNS))
	{
		// If there is already a corpse there, this corpse "overrules" the
		// older one. But if there is some other noccent there, do not cover
		// it with a corpse.
		list<NOccEnt>::iterator enoe = any_noccent_at(pos, NOE_CORPSE);
		if(enoe != noccents[NOE_CORPSE].end())
		{
			enoe->set_col(team_colour[pit->team]);
			enoe->set_misc(CORPSE_DECAY_TURNS);
		}
		else
		{
			Tile* tp = Game::curmap->mod_tile(pos);
			if(!(tp->flags & TF_NOCCENT))
			{
				noccents[NOE_CORPSE].push_back(NOccEnt(pos, '%',
					team_colour[pit->team], CORPSE_DECAY_TURNS)); 
				tp->flags |= TF_NOCCENT;
			}
		}
	}
	kill_player(pit);
}

void kill_player(const list<Player>::iterator pit)
{
	if(pit->cl_props.hp > 0)
		pit->cl_props.hp = 0; // if they're negative, we don't overwrite that
	pit->doing_a_chore = 0;
	event_set.insert(pit->own_pc->getpos());
	Tile* tp = Game::curmap->mod_tile(pit->own_pc->getpos());
	// if was carrying item, drop it
	if(pit == pl_with_item)
	{
		pl_with_item = cur_players.end();
		the_item.setpos(pit->own_pc->getpos());
		obj_sector = Game::curmap->coords_in_sector(pit->own_pc->getpos());
		tp->flags |= TF_NOCCENT;
		Game::send_team_upds(cur_players.end());
		string msg = "Treasure dropped!";
		Network::construct_msg(msg, C_PORTAL_IN_LIT);
		Network::broadcast();
	}
	
	// If was a planewalker on a trip, return to view own corpse:
	if(pit->cl == C_PLANEWALKER && pit->limiter)
		pit->own_vp->set_pos(pit->own_pc->getpos());
	// (limiter and pv are reset upon next spawn)

	tp->flags &= ~(TF_OCCUPIED);
	pit->own_pc->makevoid();
	
	pit->own_vp->move_watchers(); // remove any watchers except self
	pit->action_queue.clear();
	pit->poisoner = cur_players.end();

	// If requested to change class:
	if(pit->next_cl != pit->cl)
		Game::send_state_change(pit);

	pit->stats_i->time_played[pit->cl] += time(NULL) - pit->last_switched_cl;
	time(&(pit->last_switched_cl));
}

void player_poisoned(const list<Player>::iterator it)
{
	it->poisoner->stats_i->kills[WTK_POISON]++;
	player_death(it, " was poisoned by " + it->poisoner->nick + '.', true);
}

void process_swaps()
{
	for(list<Player>::iterator it = cur_players.begin();
		it != cur_players.end(); ++it)
	{
		if(it->is_alive() && it->wants_to_move_to != MAX_D)
		{
			Coords tarpos = it->own_pc->getpos().in(it->wants_to_move_to);
			if(Game::curmap->get_tile(tarpos).flags & TF_OCCUPIED)
			{
				// Check if there is teammate willing to swap:
				list<PCEnt>::iterator pc_it = any_pc_at(tarpos);
				if(pc_it != PCs.end())
				{
					list<Player>::iterator pp = pc_it->get_owner();
					if(pp->team == it->team
						&& (pp->wants_to_move_to == !(it->wants_to_move_to)
						|| pp->turns_without_axn >= IDLE_TURNS_TO_AUTOSWAP))
						swap_places(pp, it);
				}
			}
			else // not occupied; can move there!
			{
				move_player_to(it, tarpos, true);
				it->wants_to_move_to = MAX_D;
			}
		}
	}
}


void progress_chore(const list<Player>::iterator pit)
{
	--(pit->doing_a_chore);
	switch(pit->cl)
	{
	case C_SCOUT: // disguise
		if(!pit->doing_a_chore) // done
		{
			pit->own_pc->set_disguised(true);
			string msg = "You are now disguised as the enemy.";
			Network::construct_msg(msg, C_BROWN_PC);
			Network::send_to_player(*pit);
			event_set.insert(pit->own_pc->getpos());
		}
		break;
	case C_FIGHTER: // circle strike 
	{
		list<Player>::iterator them;
		list<PCEnt>::const_iterator pc_it;
		Coords c = pit->own_pc->getpos(), d;
		char die = 8, add = 0;
		// see if weapon has rusted:
		if(pit->cl_props.tohit != classes[pit->cl].tohit)
		{
			die -= RUST_MOD;
			add -= RUST_MOD;
		}
		for(char ch = 0; ch < 4; ++ch)
		{
			d = c.in(pit->facing);
			if((pc_it = any_pc_at(d)) != PCs.end())
			{
				them = pc_it->get_owner();
				if(test_hit(them, 5, 1, die, add) // was hit...
				&& them->cl_props.hp <= 0) // ...and died
				{
					string msg = " was sliced in half by ";
					if(pit->team == them->team)
					{
						msg += teammate_str;
						pit->stats_i->tks++;
					}
					else
						record_kill(pit, WTK_CIRCLE);
					player_death(them, msg + pit->nick + '.', true);
				}
			}
			else
				add_action_ind(d, A_MISS);
			++(pit->facing);
		}
		break;
	}
	case C_MINER: // mine
	{
		Coords c = pit->own_pc->getpos().in(pit->facing);
		if(!pit->doing_a_chore) // done
		{
			// Digging is done; outcome depends on what exactly is there:
			Tile* tp = Game::curmap->mod_tile(c);
			list<OccEnt>::iterator b_it;
			if(tp->symbol == '#') // wall; dig a passage unless NODIG
			{
				if(tp->flags & TF_NODIG)
				{
					string msg = "The wall resists.";
					Network::construct_msg(msg, C_WALL_LIT);
					Network::send_to_player(*pit);
					break;
				}
				dig_open(tp);
				finish_wall_dig(c);
			}
			// windows&doors -- same thing but no NODIG check:
			else if(tp->symbol == '|' || tp->symbol == '-' || tp->symbol == '+')
			{
				dig_open(tp);
				if(tp->symbol == '+') // since doors are not seethru
					update_lights_around(c);
				event_set.insert(c);
			}
			// Destroying a boulder:
			else if(tp->flags & TF_OCCUPIED && (b_it = any_boulder_at(c)) != boulders.end())
			{
				// boulder; made this far so just destroy it:
				boulders.erase(b_it);
				tp->flags &= ~(TF_OCCUPIED);
				event_set.insert(c);
			}
			// Carving a new boulder:
			else if(tp->flags & TF_NOCCENT
				&& any_noccent_at(c, NOE_BLOCK_SOURCE) != noccents[NOE_BLOCK_SOURCE].end())
			{
				boulders.push_back(OccEnt(c, 'O', C_WALL));
				if(tp->flags & TF_OCCUPIED) // we know it's not a boulder!
				{
					list<PCEnt>::iterator pc_it = any_pc_at(c);
					if(pc_it != PCs.end())
					{
						list<Player>::iterator pit2 = pc_it->get_owner();
						string msg = " was squashed by ";
						if(pit->team == pit2->team) // note: no team kill recorded
							msg += teammate_str;
						else
							record_kill(pit, WTK_SQUASH);
						msg += pit->nick + "\'s carving frenzy.";
						player_death(pit2, msg, false);
					}
					else // may assume it's a missile
						any_missile_at(c)->makevoid(); // tile remains occupied by the boulder
				}
				tp->flags |= TF_OCCUPIED; /* Note: set in any case; if a player
					was killed, that cleared the occupied-flag! */
				event_set.insert(c);
			}
			// else it is not possible to finish for some reason
		}
		else if(cant_dig_in(c, pit->facing))
			pit->doing_a_chore = 0; // must abort
		else
			add_sound(c, S_RUMBLE);
		break;
	}
	case C_TRAPPER: // plant trap
		if(!pit->doing_a_chore) // done
		{
			Coords c = pit->own_pc->getpos();
			string msg;
			// If there is a trap, disarm it, else plant one:
			if(Game::curmap->mod_tile(c)->flags & TF_TRAP)
			{
				traps.erase(any_trap_at(c));
				Game::curmap->mod_tile(c)->flags &= ~TF_TRAP;
				msg = "You disarm the trap.";
			}
			else
			{
				traps.push_back(Trap(c, TRAP_BOOBY, pit));
				traps.back().set_seen_by(pit->team);
				Game::curmap->mod_tile(c)->flags |= TF_TRAP;
				msg = "You set a boobytrap.";
			}
			Network::construct_msg(msg, C_BOOBY_TRAP_LIT);
			Network::send_to_player(*pit);
		}
		break;
#ifdef DEBUG
	default: // Reaching this is an error!
		to_log("Error: progress_chore called with class "
			+ lex_cast(pit->cl));
		break;
#endif
	}
}


bool missile_coll(OwnedEnt* mis, const Coords &c)
{
	Tile* tar = Game::curmap->mod_tile(c);
	if(!(tar->flags & TF_WALKTHRU))
	{
		Zap* mis_as_z;
		if(tar->symbol != '+' // zaps don't bounce off of doors!
			&& (mis_as_z = dynamic_cast<Zap*>(mis)))
		{
			// Handle zap bouncing off of walls. This is a bit complicated.
			Coords now_at = mis->getpos();
			/* We want to get the tiles marked by 1 and 2 here, and they are
			 * defined differently for diagonal/nondiagonal movement:
			      2     2#
		 	   ---#     /1
			      1    /    */
			e_Dir d = mis_as_z->get_dir();
			e_Dir dtest = d;
			add_sound(now_at.in(d), S_ZAP);
			Tile *t1, *t2;
			Coords c1, c2;
			if(d%2) // a diagonal direction
			{
				--dtest;
				t2 = Game::curmap->mod_tile((c2 = now_at.in(dtest)));
				++(++dtest);
				t1 = Game::curmap->mod_tile((c1 = now_at.in(dtest)));
			}
			else // not diagonal dir
			{
				++(++dtest);
				t1 = Game::curmap->mod_tile((c1 = c.in(dtest)));
				t2 = Game::curmap->mod_tile((c2 = c.in(!dtest)));
			}
			// If BOTH or NEITHER of 1&2 are blocked, we bounce back. If exactly one
			// is blocked, we bounce to the direction that is not blocked.
			if(bool(t1->flags & TF_WALKTHRU) != bool(t2->flags & TF_WALKTHRU))
			{
				if(t1->flags & TF_WALKTHRU) // go to t1
					++(++d);
				else // go to t2
				{
					--(--d);
					c1 = c2;
				}
				// Now 'd' holds the new movement direction and c1 is the next location.
				if(mis_as_z->bounce(d))
					return true; // didn't have energy to bounce any more
				// else: imitate moving to c1:
				mis->setpos(c1.in(!d));
				return missile_coll(mis, c1);
			}
			// else both or neither is walkthru; bounce back
			mis->setpos(c);
			return mis_as_z->bounce(!d);
		}
		//else
		mis->makevoid(); // caller has unset occupied flag
		add_action_ind(c, A_MISS);
		return true;
	}
	/*else*/ if(tar->flags & TF_OCCUPIED)
	{
		// There is something. A boulder?
		list<OccEnt>::iterator bl_it = any_boulder_at(c);
		if(bl_it != boulders.end())
		{
			// Missile hits a boulder: missile is destroyed.
			mis->makevoid(); // caller has unset occ flag for missile; for block no change
			add_action_ind(c, A_MISS);
			return true;
		}
		//else: PC?
		list<PCEnt>::iterator pc_it = any_pc_at(c);
		if(pc_it != PCs.end())
		{
			missile_hit_PC(pc_it->get_owner(), mis, false);
			return true;
		}
		// else may assume it's another missile.
		// Missile hits missile -- both are destroyed:
		mis->makevoid();
		any_missile_at(c)->makevoid();
		Game::curmap->mod_tile(c)->flags &= ~(TF_OCCUPIED);
		add_action_ind(c, A_MISS);
		return true;
	}
	return false; // no collision or bounced
}


void trap_detection(const list<Player>::iterator pit)
{
	unsigned char req_turns = BASE_TURNS_TO_DETECT_TRAPS;
	if(pit->cl == C_SCOUT) req_turns = 3*req_turns/4;
	else if(pit->cl == C_TRAPPER) req_turns /= 2;

	if(pit->turns_without_axn >= req_turns)
	{
		pit->turns_without_axn = 0; // detection counts as an action!
		// Go through traps in a radius of 3 squares:
		Coords pos = pit->own_vp->get_pos(); /* Note: using viewpoint's pos
			instead of PC's assures that planewalkers can detect traps with mind's eye. */
		Coords c;
		short f;
		char line, ind;
		for(line = 0; line < DETECT_TRAPS_RAD*8; ++line)
		{
			for(ind = 0; ind < 2*DETECT_TRAPS_RAD; ind += 2)
			{
				c.x = loslookup[DETECT_TRAPS_RAD-2][line*2*DETECT_TRAPS_RAD+ind] + pos.x;
				c.y = loslookup[DETECT_TRAPS_RAD-2][line*2*DETECT_TRAPS_RAD+ind+1] + pos.y;
				if(Game::curmap->point_out_of_map(c)
					|| !((f = Game::curmap->get_tile(c).flags) & TF_SEETHRU))
					break;
				else if(f & TF_TRAP && (req_turns < BASE_TURNS_TO_DETECT_TRAPS
					|| ((f & TF_LIT) && random()%100 < 75)
					|| (!(f & TF_LIT) && random()%100 < 60)))
				{
					any_trap_at(c)->set_seen_by(pit->team);
					event_set.insert(c);
				}
			}
		}
	} // if should detect traps
}


void arrow_fall(const OwnedEnt* arr, const Coords &c)
{
	Tile* tar = Game::curmap->mod_tile(c);
	// Arrow falling on a trap might trigger the trap:
	if(tar->flags & TF_TRAP && random()%100 < CHANCE_ARROW_TRIG_TRAP)
	{
		list<Trap>::iterator tr_it = any_trap_at(c);
		// Functionality is somewhat different from trigger_trap(...), but we
		// also repeat some functionality here...
		switch(tr_it->get_m())
		{
		case TRAP_WATER:
		case TRAP_ACID:
			add_sound(c, S_SPLASH);
			break;
		case TRAP_LIGHT:
			flash_at(c);
			break;
		case TRAP_BOOBY:
			add_sound(c, S_CREAK);
			// Boobytraps are always destroyed:
			traps.erase(tr_it);
			tar->flags &= ~(TF_TRAP);
			break;
		//case TRAP_TELE: // (detected by nothing happening!)
		case TRAP_FIREB:
			fireball_trigger(tr_it, c, arr->get_owner());
			break;
		}
	} // trap found there
	else
		add_action_ind(c, A_MISS);
}


#endif // not maptest build

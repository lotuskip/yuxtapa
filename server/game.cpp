// Please see LICENSE file.
#ifndef MAPTEST
#include "game.h"
#include "settings.h"
#include "viewpoint.h"
#include "actives.h"
#include "network.h"
#include "log.h"
#include "spiral.h"
#include "chores.h"
#include "cmds.h"
#include "../common/util.h"
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/wait.h>
#ifdef DEBUG
#include <iostream>
#endif
#ifdef TIME_SERVER_TURN
#include <sys/time.h>
#endif

extern bool intermission;

// Amounts of players in teams; keeping track of these saves some
// iteration time
unsigned char num_players[2] = { 0, 0 };

e_GameMode gamemode;
e_Dir obj_sector = MAX_D;
unsigned short tdm_kills[2]; // teams' total kills for team deathmatch

namespace
{
using namespace std;
using namespace Config;

const char MAX_PING_TURNS = 20; // 5 seconds on default settings
const char SECS_TO_SHUFFLE = 30; // by active team balance

const char POISON_EVERY_N_TURN = 40;

const char game_mode_mins[] = {
	15, // dominion
	18, // conquest
	20, // steal
	22, // destroy
	8 // team-dm
};

const string team_name[2] = { "green", "purple" };
const string team_name_long[3] = { "green team!", "purple team!", "spectators!" };
const string classnames[NO_CLASS] = { "archer", "assassin", "combat mage",
	"mindcrafter", "scout", "fighter", "miner", "healer", "wizard", "trapper",
	"planewalker" };

bool team_unbalance = false;
time_t teams_unbal_since;

enum e_InitPhase {
	IPH_GEN_MAP=0, // generate static map (can take a while)
	IPH_GENERALS, // init misc stuff, mainly objective related
	IPH_PLACEMENT, // place objects (can take a while)
	IPH_MINIMAP, // generate mini view of the map and send it to players
	IPH_LIGHTMAP, // generate static lightmap
	IPH_PLAYERS, // pre-game actions for players
	IPH_DONE
};

e_InitPhase init_phase = IPH_GEN_MAP;

unsigned char spawn_cycle;  // turns between spawns
const unsigned char MIN_SPAWN_CYCLE = 40; // 10 seconds on normal settings
const unsigned char MAX_SPAWN_CYCLE = 120; // half a minute
const unsigned char AVG_SC_MAPSIZE = 150; 

SerialBuffer miniv_buf;
char renderbuffer[BUFFER_SIZE];

// objective stuff:
string objective_str = "Not played yet";
unsigned short boulders_left;

bool check_class_limit(const e_Team t, const e_Class c)
{
	if(int_settings[IS_CLASSLIMIT])
	{
		unsigned char num = 0;
		for(list<Player>::const_iterator it = cur_players.begin();
			it != cur_players.end(); ++it)
		{
			// Note: checking next_cl, not current class!
			if((it->team == t || it->team == T_SPEC) && it->next_cl == c
				&& ++num == int_settings[IS_CLASSLIMIT])
				return true;
		}
	}
	return false;
}


void active_teambal_check()
{
	if(Config::int_settings[IS_TEAMBALANCE] == TB_ACTIVE)
	{
		if(abs(num_players[T_GREEN] - num_players[T_PURPLE]) >= 2)
		{
			// teams are unbalanced; were they already?
			if(!team_unbalance)
			{
				team_unbalance = true;
				time(&teams_unbal_since);
			}
			// in any case, give the request:
			Network::to_chat("The teams are unbalanced, please fix them!");
		}
		else // teams are balanced (once more)
			team_unbalance = false;
	}
}


void send_state_upd(const list<Player>::iterator it)
{
	Network::send_buffer.clear();
	Network::send_buffer.add((unsigned char)MID_STATE_UPD);
	Network::send_buffer.add(
		static_cast<unsigned char>(it->cl_props.hp));
	Network::send_buffer.add(
		static_cast<unsigned char>(it->cl_props.tohit));
	Network::send_buffer.add(
		static_cast<unsigned char>(it->cl_props.dam_add));
	Network::send_buffer.add((unsigned char)it->cl_props.dv);
	Network::send_buffer.add((unsigned char)(it->poisoner != cur_players.end()));
	Network::send_buffer.add((unsigned char)it->sector);
	Network::send_buffer.add(it->torch_symbol(it->is_alive() && it->own_pc->torch_is_lit()));
	Network::send_to_player(*it);
	it->needs_state_upd = false;
}

void send_full_state_refresh(const list<Player>::iterator it)
{
	Game::send_state_change(it);
	if(it->team != T_SPEC && it->next_cl == it->cl)
		send_state_upd(it);
}


void validate_and_process(const Axn &axn, const list<Player>::iterator pit)
{
	if(axn.xncode <= XN_MOVE // these are always "sensible"
		|| short(axn.xncode - XN_SHOOT) == short(pit->cl))
		process_action(axn, pit);
	else
	{
		// Resend class info; ("client plays a healer but is sending a
		// shoot action; it must think it is still playing an archer!")
		send_full_state_refresh(pit);
		pit->action_queue.clear(); // discard rest of so-far-received actions, too
	}
}

void post_join_msg(const list<Player>::const_iterator pit)
{
	Network::to_chat(pit->nick + " has joined the "
		+ team_name_long[pit->team]);
}

void post_spawn_msg(const list<Player>::const_iterator pit)
{
	string s = "You will spawn as a " + team_name[pit->team];
	s += ' ';
	s += classnames[pit->next_cl];
	s += '.';
	Network::construct_msg(s, team_colour[pit->team]);
	Network::send_to_player(*pit);
}


void player_left_team(const list<Player>::iterator pit)
{
	pit->own_vp->move_watchers();
	if(!intermission)
	{
		if(pit->is_alive())
			kill_player(pit);
		Game::del_owneds(pit);
		Game::send_times(pit);
	}
	post_join_msg(pit);
	Game::send_team_upds(cur_players.end());
	active_teambal_check();
}

// Call when player 'pit' switched to follow player 'fit'
void follow_change(const list<Player>::iterator pit,
	const list<Player>::const_iterator fit)
{
	pit->viewn_vp->drop_watcher(pit->stats_i->ID);
	(pit->viewn_vp = fit->own_vp)->add_watcher(pit);
	if(pit->viewn_vp != pit->own_vp)
	{
		string s = "Now following " + fit->nick + ", a "
			+ team_name[fit->team]
			+ ' ' + classnames[fit->cl] + '.';
		Network::construct_msg(s, C_FIREB_TRAP_LIT);
		Network::send_to_player(*pit);
	}
}


void upd_num_boulders()
{
	boulders_left = 0;
	for(list<OccEnt>::const_iterator b_it = boulders.begin();
		b_it != boulders.end(); ++b_it)
	{
		if(!b_it->isvoid() &&
			Game::curmap->coords_in_sector(b_it->getpos()) == obj_sector)
			++boulders_left; // a boulder is in the target sector
	}
}


// Spawn entire team; returns true if this team has no flags and thus has lost
bool spawn_team(const e_Team t)
{
	// If there are no flags, this means the other team has won!
	if(team_flags[t].empty())
		return true;

	// Do the check for boulder destruction:
	if(gamemode == GM_DESTR && t == T_PURPLE)
	{
		unsigned short obl = boulders_left;
		upd_num_boulders();
		if(!boulders_left)
		{
			team_flags[T_PURPLE].clear();
			return true; // Green has won!
		}
		if(obl != boulders_left) // amount changed!
			Game::send_team_upds(cur_players.end());
	}

	list<list<NOccEnt>::iterator>::const_iterator fit
		= --(team_flags[t].end());
	char spawned = 0;

	nearby_set_dim(Game::curmap->get_size());
	Coords c = (*fit)->getpos();
	init_nearby(c);

	for(list<Player>::iterator it = cur_players.begin();
		it != cur_players.end(); ++it)
	{
		// Player is to spawn if she is in this team and dead:
		if(it->team == t && !it->is_alive())
		{
			// Spawn this player around 'c';
			// For spawning, must be unoccupied & walkthru
			while(Game::curmap->get_tile(c).flags
				& (TF_OCCUPIED|TF_KILLS|TF_DROWNS)
				|| !(Game::curmap->get_tile(c).flags & TF_WALKTHRU))
				c = next_nearby();
			Game::curmap->mod_tile(c)->flags |= TF_OCCUPIED;
			PCs.push_back(PCEnt(c, it));
			it->own_pc = &(*(--PCs.end()));
			it->own_vp->set_pos(c);
			it->sector = Game::curmap->coords_in_sector(c);
			follow_change(it, it);

			it->action_queue.clear();
			it->set_class();
			it->own_vp->set_losr(it->cl_props.losr);
			it->init_torch();
			send_state_upd(it);

			// After placing 9 players, place the next 9 at the next spawn
			// place, if any:
			if(++spawned == 9)
			{
				if(fit == team_flags[t].begin())
					fit = --(team_flags[t].end());
				else
					--fit;
				c = (*fit)->getpos();
				init_nearby(c);
				spawned = 0;
			}
			else
				c = next_nearby();
		} // player in this team and dead
	} // go through players
	return false;
}


} // end local namespace

unsigned short map_over_turn = 1; /* init to nonzero, so that
	(curturn == map_over_turn) is false initially. */

namespace Game
{
	unsigned short curturn = 0;
	Map *curmap = 0;
}


void Game::next_map(const string &loadmap)
{
	// "fast-kill" all players and record playing times:
	for(list<Player>::iterator it = cur_players.begin();
		it != cur_players.end(); ++it)
	{
		if(it->is_alive())
		{
			it->cl_props.hp = it->doing_a_chore = 0;
			it->poisoner = cur_players.end();
			it->stats_i->time_played[it->cl] += time(NULL)
				- it->last_switched_cl;
		}
		else if(it->team == T_SPEC)
			it->stats_i->time_specced += time(NULL) - it->last_switched_cl;
		time(&(it->last_switched_cl));
		send_full_state_refresh(it);
	}
	// Clear EVERYTHING: (the rest of EVERYTHING is cleared in do_placement)
	axn_indicators.clear();
	event_set.clear();
	reg_sounds.clear();
	voices.clear();
	fball_centers.clear();

	construct_team_msgs(cur_players.end());
	send_team_upds(cur_players.end());

	// ready for next generation:
	if(loadmap.empty())
		init_phase = IPH_GEN_MAP;
	else if(!curmap->load_from_file(loadmap))
	{
		Network::to_chat("Could not load map by name \'" + loadmap + "\'!");
		init_phase = IPH_GEN_MAP;
	}
	else // no need to generate map!
	{
		Network::to_chat("Loaded map \'" + loadmap + "\'.");
		init_phase = IPH_GENERALS;
	}
}

void Game::init_game()
{
	switch(init_phase)
	{
	case IPH_DONE: return;
	case IPH_GEN_MAP:
		// generate a map:
		if(curmap)
			delete curmap;
		curmap = new Map(Config::int_settings[Config::IS_MAPSIZE],
			Config::int_settings[Config::IS_MAPSIZEVAR], cur_players.size());
		break;
	case IPH_GENERALS:
		curturn = 0;
		// figure out game mode:
		gamemode = Config::next_game_mode();
		// base spawn cycle length is determined by mapsize:
		spawn_cycle = (MIN_SPAWN_CYCLE - MAX_SPAWN_CYCLE)*AVG_SC_MAPSIZE/
			(curmap->get_size() + AVG_SC_MAPSIZE - 1) + MAX_SPAWN_CYCLE;
		if(gamemode == GM_DESTR) // Destroy objective has a longer spawn_cycle:
			spawn_cycle = min(6*spawn_cycle/5, int(MAX_SPAWN_CYCLE));
		else if(gamemode == GM_DOM) // Dominion has a shorter spawn_cycle:
			spawn_cycle = max(5*spawn_cycle/6, int(MIN_SPAWN_CYCLE));
		else if(gamemode == GM_TDM) // Team death match has a VERY short spawn cycle:
		{
			spawn_cycle = MIN_SPAWN_CYCLE;
			tdm_kills[T_GREEN] = tdm_kills[T_PURPLE] = 0;
		}
		// Do these regardless of whether mode is steal or not:
		item_moved = false;
		pl_with_item = cur_players.end();
		break;
	case IPH_PLACEMENT:
		do_placement();
		if(gamemode == GM_DESTR)
			upd_num_boulders();
		break;
	case IPH_MINIMAP:
	{
		// normal view (2*size^2) + num of titles (1)
		//  + one title (at most [size] chars, + '\0' + 2 char coords)
		char miniview[VIEWSIZE*VIEWSIZE*2+VIEWSIZE+4];
		short bufsize = VIEWSIZE*VIEWSIZE*2+4; // all except the title (the size of which varies)
		Game::curmap->gen_miniview(miniview); // raw minimap
		// add flags:
		Coords tmpc;
		for(list<NOccEnt>::const_iterator it = noccents[NOE_FLAG].begin();
			it != noccents[NOE_FLAG].end(); ++it)
		{
			// If flag is at (x,y), it is placed on the minimap at
			// (x*VIEWSIZE/mapsize, y*VIEWSIZE/mapsize) =: (x',y').
			// Now, the point (x',y') is in the buffer at (y'*VIEWSIZE+x')*2.
			// But we can't simplify all this into a single expression, because
			// intermediate integer rounding must take place!
			tmpc = it->getpos();
			tmpc.x = tmpc.x*VIEWSIZE/Game::curmap->get_size();
			tmpc.y = tmpc.y*VIEWSIZE/Game::curmap->get_size();
			it->draw(miniview + 2*(tmpc.y*VIEWSIZE+tmpc.x), true);
		}
		miniview[VIEWSIZE*VIEWSIZE*2] = 1; // num of titles
		miniview[VIEWSIZE*VIEWSIZE*2 + 1] = VIEWSIZE/2; // x-pos
		miniview[VIEWSIZE*VIEWSIZE*2 + 2] = VIEWSIZE/2-1; // y-pos
		switch(gamemode)
		{
		case GM_DOM:
			objective_str = "Dominion";
			break;
		case GM_CONQ:
			objective_str = "Conquest";
			break;
		case GM_STEAL:
			objective_str = "Steal (" + short_sector_name[obj_sector] + ')';
			break;
		case GM_DESTR:
			objective_str = "Destroy " + short_sector_name[obj_sector] + " boulders";
			break;
		case GM_TDM:
			objective_str = "Team deathmatch";
			break;
		}
		strcpy(miniview + VIEWSIZE*VIEWSIZE*2 + 3, objective_str.c_str());
		bufsize += objective_str.size();
		// compress, so it's ready to be sent:
		miniv_buf.clear();
		miniv_buf.add((unsigned char)MID_VIEW_UPD);
		miniv_buf.add((unsigned short)0); // turn number, which must be there
		miniv_buf.write_compressed(miniview, bufsize);
		// send to all players:
		Network::broadcast(0xFFFF, miniv_buf);
		break;
	}
	case IPH_LIGHTMAP:
		for(list<NOccEnt>::const_iterator i = noccents[NOE_TORCH].begin();
			i != noccents[NOE_TORCH].end(); ++i)
			update_static_light(i->getpos());
		break;
	case IPH_PLAYERS:
		// stuff that has to be done for all players:
		for(list<Player>::iterator it = cur_players.begin();
			it != cur_players.end(); ++it)
		{
			it->clear_acts();
			it->last_heard_of = 0;
			it->own_vp->newmap();
		}
		break;
	}
	init_phase = e_InitPhase(init_phase+1);
}


void Game::start()
{
	// It is possible that !nextmap is forced by a player before
	// the map is ready. Hence this check:
	while(init_phase != IPH_DONE)
		init_game();

	// Base maptime is [n] minutes where n depends on gamemode:
	map_over_turn = game_mode_mins[gamemode]
		*60*1000/Config::int_settings[Config::IS_TURNMS];
	// But we enforce a lower/upper limit dependent on map size:
	if(map_over_turn < 14*curmap->get_size())
		map_over_turn = 14*curmap->get_size();
	// (The range here is roughly 2--30 minutes on normal settings.)
	else if(map_over_turn > 15*curmap->get_size() + 2900)
		map_over_turn = 15*curmap->get_size() + 2900;
	// (And here roughly 14--44 minutes)
	
	if(gamemode == GM_STEAL)
		objective_str.erase(5); /* leaving just "Steal"; from now on the
		sector is given in the status string */

	send_times(cur_players.end());
	send_team_upds(cur_players.end());
	send_flag_upds(cur_players.end());
	for(list<Player>::iterator it = cur_players.begin();
		it != cur_players.end(); ++it)
	{
		// reduce intermission time from total time spent on server:
		it->stats_i->last_seen += time(NULL) - it->last_switched_cl;
		time(&(it->last_switched_cl));
	}
}


bool Game::process_turn()
{
	/* Turn processing process:
	 * 1. check teammates wanting to change places
	 * 2. update each entity, process poisonings
	 * 3. do "garbage collection"
	 * 4. process spawning
	 * 5. process an action in each player's action queue, if any
	 * 6. render the situation to the players
	 * 7. possible active teambalance check
	 * 8. see if map should end
	 */
#ifdef TIME_SERVER_TURN
	// Timing the turn process, for performance purposes:
	timeval tv;	
	long start_usecs;
	gettimeofday(&tv, 0);
	start_usecs = tv.tv_sec*1000000 + tv.tv_usec;
#endif

	// All player movement actions have been processed, finish by checking
	// teammates swapping places:
	process_swaps();

	// Update entities that need it:
	list<Arrow>::iterator a_it; 
	for(a_it = arrows.begin(); a_it != arrows.end(); ++a_it)
		a_it->update();
	list<MM>::iterator m_it; 
	for(m_it = MMs.begin(); m_it != MMs.end(); ++m_it)
		m_it->update();
	list<Zap>::iterator z_it; 
	for(z_it = zaps.begin(); z_it != zaps.end(); ++z_it)
		z_it->update();
	list<NOccEnt>::iterator ne_it;
	for(ne_it = noccents[NOE_CORPSE].begin();
		ne_it != noccents[NOE_CORPSE].end(); ++ne_it)
		ne_it->update();

	// Check poisoning:
	list<Player>::iterator it;
	if(!(curturn % POISON_EVERY_N_TURN))
	{
		for(it = cur_players.begin(); it != cur_players.end(); ++it)
		{
			// a valid poisoner implies player is dead!
			if(it->poisoner != cur_players.end())
			{
				if(--(it->cl_props.hp) <= 0)
					player_poisoned(it);
				else
					it->needs_state_upd = true;
			}
		}
	}

	// Destroy voided entities: (perhaps due to bad design, I don't see an easy
	// way to use templates here, so it's kind of repetitive...)
	a_it = arrows.begin();
	while(a_it != arrows.end())
	{
		if(a_it->isvoid()) a_it = arrows.erase(a_it);
		else ++a_it;
	}
	m_it = MMs.begin();
	while(m_it != MMs.end())
	{
		if(m_it->isvoid()) m_it = MMs.erase(m_it);
		else ++m_it;
	}
	z_it = zaps.begin();
	while(z_it != zaps.end())
	{
		if(z_it->isvoid()) z_it = zaps.erase(z_it);
		else ++z_it;
	}
	list<PCEnt>::iterator pc_it = PCs.begin();
	while(pc_it != PCs.end())
	{
		if(pc_it->isvoid()) pc_it = PCs.erase(pc_it);
		// With PCs, save time and update torches at the same time:
		else
		{
			if(pc_it->torch_is_lit())
			{
				if(pc_it->get_owner()->burn_torch())
				{
					string msg = "Suddenly your torch burns out.";
					Network::construct_msg(msg, C_LIGHT_TRAP_LIT);
					Network::send_to_player(*(pc_it->get_owner()));
					pc_it->toggle_torch();
					event_set.insert(pc_it->getpos());
				}
			}
			++pc_it;
		}
	}
	list<OccEnt>::iterator b_it = boulders.begin();
	while(b_it != boulders.end())
	{
		if(b_it->isvoid()) b_it = boulders.erase(b_it);
		else ++b_it;
	}
	ne_it = noccents[NOE_CORPSE].begin();
	while(ne_it != noccents[NOE_CORPSE].end())
	{
		if(ne_it->isvoid()) ne_it = noccents[NOE_CORPSE].erase(ne_it);
		else ++ne_it;
	}
	
	// Handle spawning:
	if(!(curturn % spawn_cycle))
	{
		if(spawn_team(T_PURPLE))
			return true;
	}
	else if(!((curturn + spawn_cycle/2) % spawn_cycle))
	{
		if(spawn_team(T_GREEN))
			return true;
	}
	
	// Go through non-spectators, checking who acted and who didn't:
	for(it = cur_players.begin(); it != cur_players.end(); ++it)
	{
		// If we haven't heard from a player for MAX_PING_TURNS turns,
		// assume that the client has disconnected.
		if(curturn - it->last_heard_of > MAX_PING_TURNS)
		{
			it = remove_player(it, " dropped.");
			continue;
		}

		if(it->is_alive())
		{
			// First check for drowning. If is in water and could've
			// moved away, drowning damage.
			if(!it->wait_turns &&
				(curmap->mod_tile(it->own_pc->getpos())->flags & TF_DROWNS))
			{
				string s = "You are drowning!";
				Network::construct_msg(s, C_WATER_LIT);
				Network::send_to_player(*it);
				it->cl_props.hp -= random()%(classes[it->cl].hp/2 + (classes[it->cl].hp%2));
				if(it->cl_props.hp <= 0)
				{
					player_death(it, " drowned.", false);
					continue;
				}
				// else (needs_state_upd is set in player_death otherwise)
				it->needs_state_upd = true;
				// splashing sounds for drowning regardless of class:
				add_sound(it->own_pc->getpos(), S_SPLASH);
			}
			// if here, did not die of drowning:
			if(!it->acted_this_turn)
			{
				if(it->wait_turns) // lost turn(s) due to something
					it->wait_turns--;
				else if(it->doing_a_chore) // working on something
					progress_chore(it);
				else if(it->action_queue.empty())
				{
					if(it->cl != C_ASSASSIN && it->cl != C_PLANEWALKER
						&& it->limiter)
						it->limiter--;
					// Being blind prevents trap detection and is not considered idling:
					if(!it->own_vp->is_blind())
					{
						it->turns_without_axn++;
						trap_detection(it);
					}
				}
				else
				{
					validate_and_process(it->action_queue.front(), it);
					if(!it->action_queue.empty()) /* Might've been cleared
							in processing! */
						it->action_queue.pop_front();
				}
			}
			else
			{
				it->acted_this_turn = false; // for next turn
				it->turns_without_axn = 0;
			}
		} // if alive
	} // loop players
	
	/// Render&Send
	using Network::send_buffer;
	vector<string> shouts;
	short i, j;
	for(it = cur_players.begin(); it != cur_players.end(); ++it)
	{
		// check if this player's viewpoint has any watchers (if not, no point
		// in rendering!)
		if(it->own_vp->has_watchers())
		{
			send_buffer.clear();
			send_buffer.add((unsigned char)MID_VIEW_UPD);
			send_buffer.add(curturn);

			/* Check if there have been any events during this turn in the
			 * vicinity of this viewpoint. If there are none, we don't
			 * need to render, but must anyway send a "tick message"
			 * indicating that a turn has passed. Also always rerender for
			 * poisoned PCs. */
			if(it->own_vp->pos_changed() || events_around(it->own_vp->get_pos())
				|| it->poisoner != cur_players.end())
			{
				i = it->own_vp->render(renderbuffer, shouts);
				if(it->poisoner != cur_players.end())
				{
					// if poisoned, draw random tiles sickly green
					for(j = 0; j < 2*VIEWSIZE*VIEWSIZE; j += 2)
					{
						if(renderbuffer[j+1] != '?' && !(random()%3))
							renderbuffer[j] = C_ACID_TRAP;
					}
				}
				send_buffer.write_compressed(renderbuffer, i);
			}

			// Send to all watchers:
			for(vector< list<Player>::iterator >::const_iterator wi =
				it->own_vp->foll_beg(); wi != it->own_vp->foll_end(); ++wi)
				Network::send_to_player(**wi);
			// Send possible shouts:
			while(!shouts.empty())
			{
				Network::construct_msg(shouts.back(), DEF_MSG_COL);
				for(vector< list<Player>::iterator >::const_iterator wi =
					it->own_vp->foll_beg(); wi != it->own_vp->foll_end(); ++wi)
					Network::send_to_player(**wi);
				shouts.pop_back();
			}
		}
		// If the viewpoint is blinded, reduce time left:
		it->own_vp->reduce_blind();
		// Check if the player needs a state update:
		if(it->needs_state_upd)
			send_state_upd(it);
	}
	event_set.clear();
	clear_action_inds();
	clear_sounds();
	fball_centers.clear();

	// Teams need shuffled?
	if(team_unbalance && time(NULL) - teams_unbal_since >= SECS_TO_SHUFFLE)
	{
		shuffle_teams();
		team_unbalance = false;
	}

#ifdef TIME_SERVER_TURN
	// Timing the turn process, for performance purposes:
	gettimeofday(&tv, 0);
	cout << tv.tv_sec*1000000 + tv.tv_usec - start_usecs << "µs" << endl;
#endif

	return (++curturn == map_over_turn);
}


SerialBuffer &Game::get_miniview() { return miniv_buf; }


void Game::player_action(const list<Player>::iterator pit, const Axn action)
{
	// A spectator is outside of the rules logic, but can only move:
	if(pit->team == T_SPEC)
	{
		// Wants to move and is a "free spectator":
		if(action.xncode == XN_MOVE && pit->viewn_vp == pit->own_vp)
			pit->own_vp->move(e_Dir(action.var1));
		// Wants to switch followed player:
		else if(action.xncode == XN_FOLLOW_SWITCH)
		{
			list<Player>::const_iterator fit = pit->viewn_vp->get_owner();
			// follow next nonspectator (but allow following self)
			do {
				if(++fit == cur_players.end())
					fit = cur_players.begin();
			} while(!fit->is_alive() && fit != pit);
			follow_change(pit, fit);
		}
		else if(action.xncode == XN_FOLLOW_PREV)
		{
			list<Player>::const_iterator fit = pit->viewn_vp->get_owner();
			// follow prev nonspectator (but allow following self)
			do {
				if(fit == cur_players.begin())
					fit = cur_players.end();
				--fit;
			} while(!fit->is_alive() && fit != pit);
			follow_change(pit, fit);
		}
		return;
	}
	// Not spectator:
	if(pit->is_alive())
	{
		// Planewalkers' mind's eye movement is not restricted by timing:
		if(pit->cl == C_PLANEWALKER && pit->limiter && action.xncode == XN_MOVE)
		{
			pit->own_vp->move(e_Dir(action.var1));
			return;
		}
		// Everything else:
		if(action.xncode == XN_FOLLOW_SWITCH || action.xncode == XN_FOLLOW_PREV)
		{
			// these codes makes no sense when alive; resend class info
			send_full_state_refresh(pit);
			return;
		}
		// If this players hasn't acted this turn, we process this new action
		// immediately and empty the queue. Otherwise, we just push it to
		// the back of the queue.
		if(!pit->acted_this_turn && !pit->wait_turns)
		{
			pit->doing_a_chore = 0; // any chore is aborted
			validate_and_process(action, pit);
			pit->action_queue.clear();
		}
		else
			pit->action_queue.push_back(action);
		// Note that the possibility of the queue being full has already been
		// checked in network.cpp
	}
	else // player is dead
	{
		if(action.xncode == XN_FOLLOW_SWITCH)
		{
			list<Player>::const_iterator fit = pit->viewn_vp->get_owner();
			// follow next alive teammate or self:
			do {
				if(++fit == cur_players.end())
					fit = cur_players.begin();
			} while(fit != pit && (!fit->is_alive() || fit->team != pit->team));
			follow_change(pit, fit);
		}
		else if(action.xncode == XN_FOLLOW_PREV)
		{
			list<Player>::const_iterator fit = pit->viewn_vp->get_owner();
			// follow prev alive teammate or self:
			do {
				if(fit == cur_players.begin())
					fit = cur_players.end();
				--fit;
			} while(fit != pit && (!fit->is_alive() || fit->team != pit->team));
			follow_change(pit, fit);
		}
		else // client apparently thinks it's alive:
			send_full_state_refresh(pit);
	}
}


void Game::construct_team_msgs(const list<Player>::const_iterator to)
{
	string teamstrs[3] = { "Greens:", "Purples:", "Spectators:" };
	for(list<Player>::const_iterator it = cur_players.begin();
		it != cur_players.end(); ++it)
	{
		teamstrs[it->team] += ' ' + it->nick;
		if(it->botpid != -1)
			teamstrs[it->team] += " (B)";
		else
			teamstrs[it->team] += admin_lvl_str[it->stats_i->ad_lvl];
	}
	
	for(char c = 0; c < 3; ++c)
	{
		if(teamstrs[c][teamstrs[c].size()-1] == ':') // no players
			teamstrs[c] += " -";
		Network::construct_msg(teamstrs[c], team_colour[c]);
		if(to == cur_players.end())
			Network::broadcast();
		else
			Network::send_to_player(*to);
	}
}

void Game::send_team_upds(const list<Player>::const_iterator to)
{
	Network::send_buffer.clear();
	Network::send_buffer.add((unsigned char)MID_GAME_UPD);
	Network::send_buffer.add(num_players[T_GREEN]);
	Network::send_buffer.add(num_players[T_PURPLE]);
	Network::send_buffer.add(objective_str);

	string obj_status_str;
	if(intermission)
	{
		// Check what happened; by default we assume a tie:
		obj_status_str = ", ";
		// Conditions under which purple wins:
		if(team_flags[T_GREEN].empty()
			|| (curturn == map_over_turn && gamemode != GM_DOM && gamemode != GM_TDM)
			|| (gamemode == GM_TDM && tdm_kills[T_PURPLE] > tdm_kills[T_GREEN]))
			obj_status_str += str_team[T_PURPLE];
		// Conditions under which green wins:
		else if(team_flags[T_PURPLE].empty()
			|| (gamemode == GM_TDM && tdm_kills[T_GREEN] > tdm_kills[T_PURPLE]))
			obj_status_str += str_team[T_GREEN];
		else
			obj_status_str += "neither team";
		obj_status_str += " won";
		if(gamemode == GM_TDM)
		{
			if(tdm_kills[T_GREEN] == tdm_kills[T_PURPLE])
				obj_status_str += "; score "
					+ lex_cast(tdm_kills[T_GREEN]) + " even";
			else
				obj_status_str += " by "
					+ lex_cast(max(tdm_kills[T_PURPLE],tdm_kills[T_GREEN]))
					+ " to " + lex_cast(min(tdm_kills[T_PURPLE],tdm_kills[T_GREEN]));
		}
		obj_status_str += '!';
	}
	else // not intermission
	{
		switch(gamemode)
		{
		case GM_DOM: 
		case GM_CONQ:
			if(team_flags[T_PURPLE].size() > team_flags[T_GREEN].size())
				obj_status_str = ", Purple has "
					+ lex_cast(team_flags[T_PURPLE].size());
			else
				obj_status_str = ", Green has "
					+ lex_cast(team_flags[T_GREEN].size());
			obj_status_str += '/';
			obj_status_str += lex_cast(noccents[NOE_FLAG].size()); 
			break;
		case GM_STEAL:
			obj_status_str = " (" + long_sector_name[obj_sector] + ") item ";
			if(pl_with_item != cur_players.end())
				obj_status_str += "stolen";
			else if(item_moved)
				obj_status_str += "dropped";
			else
				obj_status_str += "secure";
			break;
		case GM_DESTR:
			obj_status_str = ", " + lex_cast(boulders_left)
				+ " left";
			break;
		case GM_TDM:
			obj_status_str = ", ";
			if(tdm_kills[T_GREEN] == tdm_kills[T_PURPLE])
				obj_status_str += "tie!";
			else
				obj_status_str += str_team[(tdm_kills[T_PURPLE] > tdm_kills[T_GREEN])] + " is in the lead";
			break;
		default: obj_status_str = ""; break;
		}
	}
	Network::send_buffer.add(obj_status_str);

	if(to == cur_players.end())
		Network::broadcast();
	else
		Network::send_to_player(*to);
}


void Game::send_flag_upds(const list<Player>::const_iterator to)
{
	unsigned char flagbuf[MAX_D+1] = { MAX_PREDEF_CPAIR, MAX_PREDEF_CPAIR,
		MAX_PREDEF_CPAIR, MAX_PREDEF_CPAIR, MAX_PREDEF_CPAIR, MAX_PREDEF_CPAIR,
		MAX_PREDEF_CPAIR, MAX_PREDEF_CPAIR, MAX_PREDEF_CPAIR };
	char ch, bp;
	for(list<NOccEnt>::const_iterator it = noccents[NOE_FLAG].begin();
		it != noccents[NOE_FLAG].end(); ++it)
	{
		ch = Game::curmap->coords_in_sector(it->getpos());
		if(it->get_m() == T_NO_TEAM) // neutral flag
			flagbuf[ch] = C_NEUT_FLAG;
		else
		{
			bp = it->get_m();
			flagbuf[ch] = C_GREEN_PC
				+ bp // +1 if purple
				+ NUM_NORM_COLS*(it == *(--(team_flags[bp].end()))); // make lit if spawn flag
		}
	}

	Network::send_buffer.clear();
	Network::send_buffer.add((unsigned char)MID_FLAG_UPD);
	for(ch = 0; ch <= MAX_D; ++ch)
		Network::send_buffer.add(flagbuf[ch]);

	if(to == cur_players.end()) // send to all
		Network::broadcast();
	else // only send to 'to'
		Network::send_to_player(*to);
}


void Game::send_times(const list<Player>::const_iterator to)
{
	// A convenient place as any to do a periodic check on what bots have
	// finished (or crashed or been killed or ...):
	num_bots();

	Network::send_buffer.clear();
	// mid and maptime are the same for all clients:
	Network::send_buffer.add((unsigned char)MID_TIME_UPD);
	Network::send_buffer.add((unsigned short)(
		(map_over_turn - curturn)*int_settings[IS_TURNMS]/1000));
	// but the spawn time is team-specific!
	unsigned char sts[3] = {
		// green spawn time:
		(unsigned char)((spawn_cycle - ((curturn + spawn_cycle/2) % spawn_cycle))
			*int_settings[IS_TURNMS]/1000),
		// purple spawn time:
		(unsigned char)((spawn_cycle - (curturn % spawn_cycle))*int_settings[IS_TURNMS]/1000),
		0 }; // specs don't spawn

	if(to == cur_players.end()) // need to send to all
	{
		for(list<Player>::const_iterator i = cur_players.begin();
			i != cur_players.end(); ++i)
		{
			Network::send_buffer.set_amount(3); // reset to overwrite
			Network::send_buffer.add(sts[i->team]);
			Network::send_to_player(*i);
		}
	}
	else
	{
		Network::send_buffer.add(sts[to->team]);
		Network::send_to_player(*to);
	}
}


void Game::send_state_change(const list<Player>::const_iterator to)
{
	Network::send_buffer.clear();
	Network::send_buffer.add((unsigned char)MID_STATE_CHANGE);
	Network::send_buffer.add((unsigned char)to->next_cl);
	Network::send_buffer.add((unsigned char)to->team);
	Network::send_to_player(*to);
}

void Game::send_team_info(const std::list<Player>::const_iterator to)
{
	// We will reuse renderbuffer for sending (it is vacant space when not
	// rendering).
	char *p = &(renderbuffer[1]);
	if(to->team == T_SPEC)
		renderbuffer[0] = cur_players.size() - num_players[0] - num_players[1];
	else
		renderbuffer[0] = num_players[to->team];
	if(renderbuffer[0] > VIEWSIZE) // can't print more than this
		renderbuffer[0] = VIEWSIZE;
	int count = renderbuffer[0];
	for(list<Player>::const_iterator it = cur_players.begin();
		it != cur_players.end(); ++it)
		if(it->team == to->team)
		{
			strcpy(p, it->nick.c_str());
			p += it->nick.length(); // skip '\0'
			if(it->botpid != -1)
			{
				strcpy(p, " (B)");
				p += 5; // include '\0'
			}
			else
			{
				strcpy(p, admin_lvl_str[it->stats_i->ad_lvl].c_str());
				p += admin_lvl_str[it->stats_i->ad_lvl].length()+1;
			}
			*(p++) = it->cl; // class
			if(it->is_alive()) // sector
				*(p++) = it->sector;
			else
				*(p++) = -1; // signifies dead
			if(!(--count))
				break;
		}
	Network::send_buffer.clear();
	Network::send_buffer.add((unsigned char)MID_TEAM_INFO);
	Network::send_buffer.write_compressed(renderbuffer, p - renderbuffer);
	Network::send_to_player(*to);
}


void Game::class_switch(const list<Player>::iterator pit, e_Class newcl)
{
	if(newcl == NO_CLASS) // going spectator / resetting spectating state
	{
		if(pit->team != T_SPEC)
		{
			pit->own_vp->move_watchers();
			if(pit->is_alive())
				kill_player(pit); /* see player_left_team; it won't call this
				after we've set the team to spec! */
			--num_players[pit->team];
			pit->team = T_SPEC;
			player_left_team(pit);
			pit->cl = pit->next_cl = NO_CLASS;
			send_state_change(pit);
			pit->own_vp->set_losr(SPEC_LOS_RAD);
		}

		// place own viewpoint at the location of the viewn viewpoint:
		pit->own_vp->set_pos(pit->viewn_vp->get_pos());
		follow_change(pit, pit);
	}
	else if(newcl != pit->cl || newcl != pit->next_cl)
	{
		e_Team newteam = (pit->team != T_SPEC) ? pit->team
			: e_Team(num_players[T_PURPLE] < num_players[T_GREEN]);
		
		/* Class limit checking. In case the desired class is unavailable,
		 * human players (or what we think are human players...) just get
		 * a message and nothing happens. But for bots we just give some
		 * other class. */
		string s;
		while(check_class_limit(newteam, newcl))
		{
			// In case teams are even and player is joining a team, check
			// the other team, too:
			if(pit->team == T_SPEC && num_players[T_PURPLE] == num_players[T_GREEN]
				&& !check_class_limit(T_PURPLE, newcl))
			{
				newteam = T_PURPLE;
				break; // okay to join the purple team with this class
			}
			// else
			if(pit->botpid == -1) // not bot
			{
				s = "That class is not available.";
				Network::construct_msg(s, C_FIREB_TRAP_LIT);
				Network::send_to_player(*pit);
				return;
			}
			// else
			if((newcl = e_Class(newcl+1)) == NO_CLASS)
				newcl = C_ARCHER;
		}
		// if here, okay to change class/team
		if(pit->team == T_SPEC)
		{
			// Currently a spectator: join the weaker team (or green)
			++num_players[(pit->team = newteam)];
			post_join_msg(pit);
			if(!intermission)
			{
				send_times(pit);
				pit->stats_i->time_specced += time(NULL)
					- pit->last_switched_cl;
				time(&(pit->last_switched_cl));
			}
			send_team_upds(cur_players.end());
			// The ex-spectator might be following a now-enemy:
			if(pit->viewn_vp->get_owner()->team != pit->team)
				follow_change(pit, pit);
			pit->own_vp->clear_memory();
		}

		pit->next_cl = newcl; // will set pit->cl = pit->next_cl at spawn
		post_spawn_msg(pit);
		if(!pit->is_alive())
			send_state_change(pit);
	}
}

void Game::team_switch(const list<Player>::iterator pit)
{
	if(pit->team != T_SPEC)
	{
		if(int_settings[IS_TEAMBALANCE] != TB_OFF
			&& num_players[pit->team] <= num_players[opp_team[pit->team]])
		{
			string s = "You cannot switch team at this time.";
		 	Network::construct_msg(s, team_colour[pit->team]);
		 	Network::send_to_player(*pit);
		}
		else // ok w.r.t. team balance
		{
			// do the change:
			--num_players[pit->team];
			++num_players[(pit->team = opp_team[pit->team])];

			// Switch class until find one not limited by classlimit (ALL of
			// them cannot be limited due to check in settings.cpp!)
			e_Class newcl = pit->next_cl;
			while(check_class_limit(pit->team, newcl))
			{
				if((newcl = e_Class(newcl + 1)) == NO_CLASS)
					newcl = e_Class(0);
			}
			pit->next_cl = newcl;

			player_left_team(pit);
			post_spawn_msg(pit);
			send_state_change(pit);
			pit->own_vp->clear_memory();
		}
	}
}


// Remove player, used when kicking, when dropping, or when the
// player disconnects
list<Player>::iterator Game::remove_player(list<Player>::iterator pit,
	const string &reason)
{
	// last_seen was updated when the player connected (and
	// adjusted after each intermission):
	if(intermission)
		pit->stats_i->last_seen += time(NULL) - pit->last_switched_cl;
	else
	{
		del_owneds(pit);
		// Remove any poisoning caused by this player!
		for(list<Player>::iterator it = cur_players.begin();
			it != cur_players.end(); ++it)
		{
			if(it->poisoner == pit)
				it->poisoner = cur_players.end();
		}
	}
	pit->stats_i->total_time += time(NULL) - pit->stats_i->last_seen;
	time(&(pit->stats_i->last_seen));

	if(pit->team != T_SPEC)
	{
		pit->own_vp->move_watchers();
		--num_players[pit->team - T_GREEN];
		send_team_upds(cur_players.end());
	}

	if(pit->is_alive())
		kill_player(pit); // this records time spent as a class
	else
	{
		pit->viewn_vp->drop_watcher(pit->stats_i->ID);
		// if is spec, record time spent as spec:
		if(pit->team == T_SPEC && !intermission)
			pit->stats_i->time_specced += time(NULL) - pit->last_switched_cl;
	}
	
	// If it is a bot, kill the process and ditch stats:
	if(pit->botpid != -1)
	{
		known_players.erase(pit->stats_i);
		Network::send_buffer.clear();
		Network::send_buffer.add((unsigned char)MID_QUIT);
		Network::send_to_player(*pit);
		int cet;
		waitpid(pit->botpid, &cet, 0);
	}
	
	string msg = pit->nick + reason;
	list<Player>::iterator retval = cur_players.erase(pit);
	Network::to_chat(msg);

	if(cur_players.empty())
		timed_log("no more players, awaiting connections.");
	else
		active_teambal_check();

	return retval;
}


void Game::del_owneds(const list<Player>::const_iterator pit)
{
	// Note: this function is only called at instances when it is safe
	// to "simply remove" the entities instead of voiding them so that
	// they'd be removed later.
	list<Arrow>::iterator a_it = arrows.begin();
	while(a_it != arrows.end())
	{
		if(a_it->get_owner() == pit)
		{
			if(!a_it->isvoid())
				curmap->mod_tile(a_it->getpos())->flags &= ~(TF_OCCUPIED);
			a_it = arrows.erase(a_it);
		}
		else
			++a_it;
	}
	list<MM>::iterator m_it = MMs.begin();
	while(m_it != MMs.end())
	{
		if(m_it->get_owner() == pit)
		{
			if(!m_it->isvoid())
				curmap->mod_tile(m_it->getpos())->flags &= ~(TF_OCCUPIED);
			m_it = MMs.erase(m_it);
		}
		else
			++m_it;
	}
	list<Zap>::iterator z_it = zaps.begin();
	while(z_it != zaps.end())
	{
		if(z_it->get_owner() == pit)
		{
			if(!z_it->isvoid())
				curmap->mod_tile(z_it->getpos())->flags &= ~(TF_OCCUPIED);
			z_it = zaps.erase(z_it);
		}
		else
			++z_it;
	}
	list<Trap>::iterator tr_it = traps.begin();
	while(tr_it != traps.end())
	{
		if(tr_it->get_owner() == pit)
		{
			// (traps are never voided)
			curmap->mod_tile(tr_it->getpos())->flags &= ~(TF_TRAP);
			tr_it = traps.erase(tr_it);
		}
		else
			++tr_it;
	}
}

#endif // not maptest build

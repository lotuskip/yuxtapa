// Please see LICENSE file.
#ifndef PLAYERS_H
#define PLAYERS_H

#include <sys/socket.h>
#include <sys/types.h>
#include <list>
#include <deque>
#include <ctime>
#include <string>
#include "../common/classes_common.h"
#include "../common/coords.h"

enum e_AdminLvl { AL_GUEST=0, AL_REG, AL_TU, AL_ADMIN, AL_SADMIN };
const std::string admin_lvl_str[5] = { " (G)", " (R)", " (TU)", " (A)", " (SA)" };

const char PASSW_LEN = 8;

// For recording different ways of earning kills:
enum { WTK_MELEE=0, WTK_ARROW, WTK_CIRCLE, WTK_ZAP, WTK_BOOBY, WTK_FIREB,
	WTK_MM, WTK_BACKSTAB, WTK_SQUASH, WTK_POISON, WTK_SCARE, MAX_WAY_TO_KILL };

/* This class holds information about a player *the server knows about*. This
 * is also the stuff that gets written into a file when the server is closed. */
struct PlayerStats
{
	unsigned short ID; // always != -1
	char password[PASSW_LEN+1];

	std::string last_known_as; // most recent nick this player used
	unsigned long deaths;
	unsigned long kills[MAX_WAY_TO_KILL];
	unsigned long tks; // team kills
	unsigned long healing_recvd;
	unsigned long total_time; // seconds
	unsigned long time_specced;
	unsigned long time_played[NO_CLASS];
	unsigned long arch_hits, arch_shots;
	unsigned long cm_hits, cm_shots;
	time_t last_seen;
	e_AdminLvl ad_lvl;

	// default constructor for new players
	PlayerStats();
};

/* This represents one un-processed action request from a player */
struct Axn
{
	Axn(const unsigned char x, const char v1 = 0, const char v2 = 0)
		: xncode(x), var1(v1), var2(v2) {}

	unsigned char xncode; // see ../common/netutils.h
	char var1, var2; // code dependent meanings
};

class ViewPoint;
class PCEnt;

/* This, then, is a player that is *currently connected*. */
struct Player
{
	Player() : own_vp(0) {} /* This is needed because the desctructor will
	'delete own_vp;', which otherwise segfaults on temporary Player objects */
	~Player();

	void clear_acts();
	bool is_alive() const;
	void set_class();

	void init_torch();
	bool burn_torch();
	unsigned char torch_symbol(const bool lit) const;

	bool nomagicres() const;

	// Basic stuff
	mutable sockaddr_storage address; /* mutable because network
		routines (C!) don't understand const pointers */
	std::list<PlayerStats>::iterator stats_i;
	std::string nick;
	unsigned short last_heard_of; /* the last turn we heard of
		this player; kind of like a "rough ping" */
	bool muted;
	e_Team team;
	e_Class cl, next_cl;
	time_t last_switched_cl; // to determine times spent as each class
	ViewPoint* own_vp; /* The viewpoint this client *owns*, not necessarily 
		the one that is rendered and sent to this client. */
	ViewPoint* viewn_vp; /* The viewpoint that is currently being viewn by
		this client */

	// Action related:
	unsigned short last_acted_on_turn;
	unsigned char last_axn_number;
	std::deque<Axn> action_queue;
	bool acted_this_turn;
	unsigned short turns_without_axn;
	e_Dir wants_to_move_to; // for swapping places with teammates
	char limiter; /* used for one purpose by mindcrafters/wizards/combat magi,
		and to another by assassins */
	char wait_turns;
	char doing_a_chore;

	// These things are only relevant if there is a PC attached to this player
	PCEnt* own_pc;	
	ClassProperties cl_props;
	// Note that e.g. the 'hp' in cl_props is the current hitpoints; maximum
	// must be retrieved from classes[cl].hp
	bool needs_state_upd;
	std::list<Player>::iterator poisoner; // poisoned <=> poisoner != cur_players.end()
	e_Dir facing;
	e_Dir sector;
	short torch_left;

	// This is -1 for humans and process pid for bots:
	int botpid;
};


void init_known_players(const bool nopurge);
void store_known_players();

void output_stats();

extern std::list<PlayerStats> known_players;
extern std::list<Player> cur_players;

enum { // Player Hello Result
	JOIN_OK_ID_OK = 0xFFFF, // fine, a revisitor
	ID_STEAL = 0XFFFE, // that ID is already playing!
	IP_BANNED = 0xFFFD,
	SERVER_FULL = 0xFFFC, // any other return value means "this is your new id"
	HIGHEST_VALID_ID = 0xFFFB };
unsigned short player_hello(const unsigned short id,
	std::string &passw, const std::string &nick, sockaddr_storage &sas);
unsigned short bot_connect(const unsigned short pid, sockaddr_storage &sas);

// to log average usage of the server:
void usage_update();
void usage_report();

std::list<sockaddr_storage> &banlist();

#endif

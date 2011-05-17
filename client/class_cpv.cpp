// Please see LICENSE file.
#include "class_cpv.h"
#include "view.h"
#include "base.h"
#include "network.h"
#include "settings.h"
#include "../common/constants.h"
#include "../common/col_codes.h"
#include <boost/lexical_cast.hpp>

extern e_ClientState clientstate;
extern bool walkmode;

namespace
{
using namespace std;

const char PC_INFO_X = 37;

e_Class myclass = NO_CLASS;
e_Team myteam = T_SPEC;

// at most 10 character long names for the classes:
const string pcview_classname[NO_CLASS] = {
	"Archer     ",
	"Assassin   ",
	"Combat Mage",
	"Mindcrafter",
	"Scout      ",
	"Fighter    ",
	"Miner      ",
	"Healer     ",
	"Wizard     ",
	"Trapper    ",
	"Planewalker" };

// variable pc properties:
char hp = 0, tohit, dam_die, dam_add, dv, pv;
bool poisoned;
e_Dir sector;
char torch_sym[2] = " ";

void fill_to_ten(string &s)
{
	while(s.size() < 10)
		s += ' ';
}


void reprint_pcinfo()
{
	string tmpstr = "(";
	tmpstr += sector_name[sector];
	tmpstr += ") ";
	Base::print_str(tmpstr.c_str(), team_colour[myteam], 52, 3, STAT_WIN, false);

	tmpstr = "HP: " + boost::lexical_cast<string>(short(hp))
		+ '/' + boost::lexical_cast<string>(short(classes[myclass].hp));
	fill_to_ten(tmpstr);
	Base::print_str(tmpstr.c_str(), team_colour[myteam], PC_INFO_X, 4, STAT_WIN, false);

	tmpstr = "DV/PV " + boost::lexical_cast<string>(short(dv))
		+ '/' + boost::lexical_cast<string>(short(pv));
	fill_to_ten(tmpstr);
	Base::print_str(tmpstr.c_str(), team_colour[myteam], PC_INFO_X, 5, STAT_WIN, false);

	tmpstr.clear();
	if(tohit > 0)
		tmpstr += '+';
	tmpstr += boost::lexical_cast<string>(short(tohit)) + ",1d"
		+ boost::lexical_cast<string>(short(dam_die));
	if(dam_add > 0)
		tmpstr += '+';
	if(dam_add)
		tmpstr += boost::lexical_cast<string>(short(dam_add));
	fill_to_ten(tmpstr);
	Base::print_str(tmpstr.c_str(), team_colour[myteam], PC_INFO_X, 6, STAT_WIN, false);

	if(poisoned)
		tmpstr = "[poisoned]";
	else
		tmpstr = "          ";
	Base::print_str(tmpstr.c_str(), team_colour[myteam], PC_INFO_X, 7, STAT_WIN, false);

	Base::print_str(torch_sym, C_TORCH, 51, 4, STAT_WIN, false);
}

} // end local namespace

Coords aimer;


void ClassCPV::move(const e_Dir d)
{
	if(clientstate == CS_NORMAL) // regular movement
		Network::send_action(XN_MOVE, d);
	else if(clientstate == CS_AIMING) // moving the aimer
	{
		aimer = aimer.in(d);
		if(aimer.x > VIEWSIZE/2) aimer.x--;
		else if(aimer.x < -VIEWSIZE/2) aimer.x++;
		if(aimer.y > VIEWSIZE/2) aimer.y--;
		else if(aimer.y < -VIEWSIZE/2) aimer.y++;
		redraw_view();
	}
	else if(clientstate == CS_DIR) // waiting for dir input (and now got it!)
	{
		if(myclass == C_FIGHTER)
			Network::send_action(XN_CIRCLE_ATTACK, d);
		else if(myclass == C_HEALER)
			Network::send_action(XN_HEAL, d);
		else if(myclass == C_MINER)
			Network::send_action(XN_MINE, d);
		else if(myclass == C_COMBAT_MAGE)
			Network::send_action(XN_ZAP, d);
		clientstate = CS_NORMAL;
	}	
	// else ignore
}


void ClassCPV::five()
{
	if(clientstate == CS_AIMING)
	{
		if(aimer.x || aimer.y)
		{
			aimer.x = aimer.y = 0;
			redraw_view();
		}
	}	
	else if(clientstate == CS_DIR && myclass == C_HEALER) 
	{
		Network::send_action(XN_HEAL, MAX_D); // heal self
		clientstate = CS_NORMAL;
	}
	// else ignore
}

void ClassCPV::space()
{
	if(!im_alive())
		Network::send_action(XN_FOLLOW_SWITCH);
	else
	{
		switch(myclass)
		{
		case C_ARCHER:
			if(clientstate == CS_AIMING)
			{
				if(aimer.x || aimer.y) // aiming *somewhere*
					Network::send_action(XN_SHOOT, aimer.x, aimer.y);
				clientstate = CS_NORMAL;
				redraw_view(); // to clear the aimer
			}
			else
			{
				clientstate = CS_AIMING;
				aimer.x = aimer.y = 0;
				redraw_view();
			}
			break;
		case C_ASSASSIN:
			Network::send_action(XN_FLASH);
			break;
		case C_MINDCRAFTER:
			Network::send_action(XN_BLINK);
			break;
		case C_SCOUT:
			Network::send_action(XN_DISGUISE);
			break;
		case C_TRAPPER:
			Network::send_action(XN_SET_TRAP);
			break;
		case C_WIZARD:
			Network::send_action(XN_MM);
			break;
		case C_PLANEWALKER:
			Network::send_action(XN_MINDS_EYE);
			break;
		default: //C_FIGHTER, C_HEALER, C_MINER, C_COMBAT_MAGE
			clientstate = CS_DIR;
			break;
		}
	} // not spectator or dead
}

void ClassCPV::follow_prev()
{
	if(!im_alive())
		Network::send_action(XN_FOLLOW_PREV);
	// else no effect
}

void ClassCPV::suicide()
{
	// ignore if dead
	if(im_alive())
	{
		Network::send_action(XN_SUICIDE);
		clientstate = CS_NORMAL;
	}	
}


void ClassCPV::state_change(const unsigned char cl, const unsigned char t)
{
	myteam = e_Team(t);
	hp = 0; // we are dead until further notice (the further notice being a status update)

	// print PC info:
	if((myclass = e_Class(cl)) == NO_CLASS) // spectator
		Base::print_str("Spectator  ", team_colour[T_SPEC], PC_INFO_X, 3, STAT_WIN, false);
	else // actual class 
	{
		Base::print_str(pcview_classname[myclass].c_str(), team_colour[myteam],
			PC_INFO_X, 3, STAT_WIN, false);
		dam_die = classes[myclass].dam_die;
		dv = classes[myclass].dv;
		pv = classes[myclass].pv;
	}
	// Clear the rest:
	for(char y = 4; y < 7; ++y)
		Base::print_str("          ", team_colour[T_SPEC], PC_INFO_X, y, STAT_WIN, false);
	Base::print_str("    ", team_colour[T_SPEC], 52, 3, STAT_WIN, false); // map area

	// class/team change requires any class-specific action to end:
	if(clientstate >= CS_AIMING)
	{
		clientstate = CS_NORMAL;
		redraw_view();
	}
}


void ClassCPV::state_upd(SerialBuffer &data)
{
	char oldhp = hp;
	hp = static_cast<char>(data.read_ch());
	// if got hurt but not dead and ouch is defined, send ouch:
	if(hp > 0 && hp < oldhp && Config::do_ouch())
		Network::send_line(Config::get_ouch(), false);
	tohit = static_cast<char>(data.read_ch());
	dam_add = static_cast<char>(data.read_ch());
	dv = data.read_ch();
	poisoned = bool(data.read_ch());
	sector = e_Dir(data.read_ch());
	torch_sym[0] = data.read_ch();

	reprint_pcinfo();

	// if died, abort any action in the client end:
	if(hp <= 0)
	{
		if(clientstate == CS_AIMING || clientstate == CS_DIR)
			clientstate = CS_NORMAL;
		Base::print_walk((walkmode = false));
	}
}


bool ClassCPV::im_alive() { return (myclass != NO_CLASS && hp > 0); }


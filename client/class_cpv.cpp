// Please see LICENSE file.
#include "class_cpv.h"
#include "view.h"
#include "base.h"
#include "network.h"
#include "../common/constants.h"
#include "../common/col_codes.h"
#include <boost/lexical_cast.hpp>
#ifdef DEBUG
#include <iostream>
#endif

extern e_ClientState clientstate;

namespace
{
using namespace std;

e_Dir last_dir = MAX_D; // for walkmode

e_Class myclass = NO_CLASS;
e_Team myteam = T_SPEC;

// at most 10 character long names for the classes:
const string pcview_classname[NO_CLASS] = {
	"Archer", "Assassin", "Combat M.", "Mindcraf.", "Scout", "Fighter",
	"Miner", "Healer", "Wizard", "Trapper" };

const unsigned char team_col[] = {
	C_NEUT_FLAG, C_GREEN_PC, C_PURPLE_PC };

// variable pc properties:
char hp = 0, tohit, dam_die, dam_add, dv, pv;
bool poisoned;
e_Dir sector;
char torch_sym[2] = " ";


void reprint_pcinfo()
{
	string tmpstr = "(";
	tmpstr += sector_name[sector];
	tmpstr += ')';
	Base::print_str(tmpstr.c_str(), team_col[myteam-1], 3, 0, PC_WIN, true);

	tmpstr = "HP: " + boost::lexical_cast<string>(short(hp))
		+ '/' + boost::lexical_cast<string>(short(classes[myclass].hp));
	Base::print_str(tmpstr.c_str(), team_col[myteam-1], 0, 3, PC_WIN, true);

	tmpstr = "DV/PV " + boost::lexical_cast<string>(short(dv))
		+ '/' + boost::lexical_cast<string>(short(pv));
	Base::print_str(tmpstr.c_str(), team_col[myteam-1], 0, 4, PC_WIN, true);

	tmpstr.clear();
	if(tohit > 0)
		tmpstr += '+';
	tmpstr += boost::lexical_cast<string>(short(tohit)) + ",1d"
		+ boost::lexical_cast<string>(short(dam_die));
	if(dam_add > 0)
		tmpstr += '+';
	if(dam_add)
		tmpstr += boost::lexical_cast<string>(short(dam_add));
	Base::print_str(tmpstr.c_str(), team_col[myteam-1], 0, 5, PC_WIN, true);

	if(poisoned)
		tmpstr = "[poisoned]";
	else
		tmpstr.clear();
	Base::print_str(tmpstr.c_str(), team_col[myteam-1], 0, 6, PC_WIN, true);

	Base::print_str(torch_sym, C_TORCH, 1, 8, PC_WIN, false);
}

} // end local namespace

Coords aimer;


void ClassCPV::move(const e_Dir d)
{
	switch(clientstate)
	{
	case CS_NORMAL: // regular movement
		Network::send_action(XN_MOVE, (last_dir = d));
		break;
	case CS_AIMING: // moving the aimer
		aimer = aimer.in(d);
		if(aimer.x > VIEWSIZE/2) aimer.x--;
		else if(aimer.x < -VIEWSIZE/2) aimer.x++;
		if(aimer.y > VIEWSIZE/2) aimer.y--;
		else if(aimer.y < -VIEWSIZE/2) aimer.y++;
		redraw_view();
		break;
	case CS_DIR: // waiting for dir input (and now got it!)
		if(myclass == C_FIGHTER)
			Network::send_action(XN_CIRCLE_ATTACK, d);
		else if(myclass == C_HEALER)
			Network::send_action(XN_HEAL, d);
		else if(myclass == C_MINER)
			Network::send_action(XN_MINE, d);
		else if(myclass == C_COMBAT_MAGE)
			Network::send_action(XN_ZAP, d);
		clientstate = CS_NORMAL;
		break;
	}
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
	else if(clientstate == CS_DIR)
	{
		if(myclass == C_HEALER)
		{
			Network::send_action(XN_HEAL, MAX_D); // heal self
			clientstate = CS_NORMAL;
		}
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
		default: //C_FIGHTER, C_HEALER, C_MINER, C_COMBAT_MAGE
			clientstate = CS_DIR;
			break;
		}
	} // not spectator or dead
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
		Base::print_str("Spectator", team_col[0], 0, 2, PC_WIN, true);
	else // actual class
	{
		Base::print_str(pcview_classname[myclass].c_str(), team_col[myteam-1],
			0, 2, PC_WIN, true);
		dam_die = classes[myclass].dam_die;
		dv = classes[myclass].dv;
		pv = classes[myclass].pv;
	}
	// Clear the rest:
	for(char y = 3; y < 7; ++y)
		Base::print_str("", team_col[0], 0, y, PC_WIN, true);
	Base::print_str("", team_col[0], 3, 0, PC_WIN, true);

	// class/team change requires any class-specific action to end:
	if(clientstate >= CS_AIMING)
	{
		clientstate = CS_NORMAL;
		redraw_view();
	}
}


void ClassCPV::state_upd(SerialBuffer &data)
{
	hp = static_cast<char>(data.read_ch());
	tohit = static_cast<char>(data.read_ch());
	dam_add = static_cast<char>(data.read_ch());
	poisoned = bool(data.read_ch());
	sector = e_Dir(data.read_ch());
	torch_sym[0] = data.read_ch();

	reprint_pcinfo();
}


bool ClassCPV::im_alive() { return (myclass != NO_CLASS && hp > 0); }


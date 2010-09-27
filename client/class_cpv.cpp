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

e_Dir prev_d; // for spellcasting
char num_spell_keys;
bool clockwise;

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


// Validate next input key for spellcasting
bool incorrect_next_d(const e_Dir d)
{
	if(clientstate == CS_CASTING_Z)
	{
		// Next dir must be the previous one turned twice clockwise:
		++(++prev_d);
		return (d != prev_d);
	}
	// else clientstate must be CS_CASTING_M:
	if(!num_spell_keys) // first dir must match last_move_dir
		return ((prev_d = d) != last_dir);
	if(num_spell_keys == 1) // this is the second key input; determines dir
	{
		e_Dir tmpdir = prev_d;
		++tmpdir;
		if(d == tmpdir)
		{
			clockwise = true;
			++prev_d;
			return false;
		} // else
		tmpdir = prev_d;
		--tmpdir;
		if(d == tmpdir)
		{
			clockwise = false;
			--prev_d;
			return false;
		}
		return true; // neither next clock- nor anticlockwise
	}
	// else direction has been determined:
	if(clockwise)
		++prev_d;
	else
		--prev_d;
	return (d != prev_d);
}

}

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
	case CS_CASTING_Z: // casting zap
		if(!num_spell_keys)
			prev_d = d; // first dir is arbitrary
		else
		{
			// Next dir must be the previous one turned twice clockwise:
			++(++prev_d);
			if(d != prev_d)
			{
				clientstate = CS_NORMAL; // messed up!
				break;
			}
		}
		if(++num_spell_keys == 4)
		{
			// Cast succesfully!
			Network::send_action(XN_ZAP, d);
			clientstate = CS_NORMAL;
		}
		break;
	case CS_CASTING_M:
		if(!num_spell_keys) // first dir must match last_move_dir
		{
			if((prev_d = d) != last_dir)
				clientstate = CS_NORMAL; // incorrect
			else
				++num_spell_keys;
			break;
		} // else
		if(num_spell_keys == 1) // this is the second key input; determines dir
		{
			e_Dir tmpdir = prev_d;
			++tmpdir;
			if(d == tmpdir)
			{
				clockwise = true;
				++prev_d;
				++num_spell_keys;
				break;
			} // else
			tmpdir = prev_d;
			--tmpdir;
			if(d == tmpdir)
			{
				clockwise = false;
				--prev_d;
				++num_spell_keys;
				break;
			}
			// neither next clock- nor anticlockwise:
			clientstate = CS_NORMAL;
			break;
		}
		// else direction has been determined:
		if(clockwise)
			++prev_d;
		else
			--prev_d;
		if(d != prev_d)
			clientstate = CS_NORMAL;
		else if(++num_spell_keys == 8)
		{
			// Cast succesfully!
			Network::send_action(XN_MM);
			clientstate = CS_NORMAL;
		}
		break;
	case CS_DIR: // waiting for dir input (and now got it!)
		if(myclass == C_FIGHTER)
			Network::send_action(XN_CIRCLE_ATTACK, d);
		else if(myclass == C_HEALER)
			Network::send_action(XN_HEAL, d);
		else if(myclass == C_MINER)
			Network::send_action(XN_MINE, d);
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
	else if(clientstate == CS_CASTING_Z || clientstate == CS_CASTING_M)
		clientstate = CS_NORMAL; // messed up!
}

void ClassCPV::space()
{
	if(myclass == NO_CLASS || hp <= 0)
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
		case C_COMBAT_MAGE:
			if(clientstate == CS_CASTING_Z)
				clientstate = CS_NORMAL; // messed up casting
			else
			{
				clientstate = CS_CASTING_Z;
				num_spell_keys = 0;
			}
			break;
		case C_FIGHTER:
		case C_HEALER:
		case C_MINER:
			clientstate = CS_DIR;
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
			if(clientstate == CS_CASTING_M)
				clientstate = CS_NORMAL; // messed up casting
			else
			{
				clientstate = CS_CASTING_M;
				num_spell_keys = 0;
			}
			break;
		}
	} // not spectator or dead
}


void ClassCPV::suicide()
{
	// ignore if spectating or hp<=0
	if(myclass != NO_CLASS && hp > 0)
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
	if(clientstate >= CS_CASTING_Z)
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


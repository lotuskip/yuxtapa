// Please see LICENSE file.
#include "view.h"
#include "base.h"
#include "class_cpv.h"
#include "../common/constants.h"
#include "../common/col_codes.h"
#include "../common/los_lookup.h"
#include "../common/coords.h"
#include "../common/utfstr.h"
#include <cstring>
#include <cstdlib>

// globals:
char viewbuffer[BUFFER_SIZE];

extern e_ClientState clientstate;
extern Coords aimer;

namespace
{
using namespace std;

const char* aimerpath = "*";
const char* aimerend = "X";

const unsigned char TITLE_CPAIR = C_BLUE_ON_MISS;

bool showtitles = false;

void draw_titles()
{
	char len;
	char *pos = &(viewbuffer[VIEWSIZE*VIEWSIZE*2+1]);
	char x,y;
	for(char num = viewbuffer[VIEWSIZE*VIEWSIZE*2]; num > 0; --num)
	{
		x = *pos;
		y = *(pos+1); // pos&pos+1 are x/y pair, string begins at pos+2.
		len = strlen(pos+2);
		// If we are alive, don't draw any title coming to the center (that
		// would be our OWN title)
		if(x != VIEWSIZE/2 || y != VIEWSIZE/2 || !ClassCPV::im_alive())
			Base::print_str(pos+2, TITLE_CPAIR, x-num_syms(string(pos+2))/2, y, VIEW_WIN);
		pos += len+3; // lenght of string + '\0' + 2 chars
	}
}

void draw_aimer()
{
	// if the radius is 0 we don't need all the fancy logic:
	if(aimer.x || aimer.y)
	{
		char rad = std::max(std::abs(aimer.x), std::abs(aimer.y));

		if(rad > 1) // rad == 1 does not require LOS-lookup, either
		{
			char line;
			if(aimer.y == -rad) line = aimer.x+rad;
			else if(aimer.x == rad) line = 3*rad+aimer.y;
			else if(aimer.y == rad) line = 5*rad-aimer.x;
			else /* aimer.x == -rad */ line = 7*rad-aimer.y;

			// Note that we follow the line only two the second last point
			for(char ind = 0; ind < 2*(rad-1); ind += 2)
			{
				Base::print_str(aimerpath, C_ARROW,
					loslookup[rad-2][line*2*rad+ind] + VIEWSIZE/2,
					loslookup[rad-2][line*2*rad+ind+1] + VIEWSIZE/2, VIEW_WIN);
			}
		}
	}
	Base::print_str(aimerend, C_ARROW_LIT, VIEWSIZE/2 + aimer.x,
		VIEWSIZE/ 2 + aimer.y, VIEW_WIN);
}

// The limbo view, also in 23*23 space:
const char limboview[VIEWSIZE*VIEWSIZE*2] = {
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',2,'.',2,'o',2,'O',2,'0',0,' ',2,'L',2,'i',2,'m',2,'b',2,'o',0,' ',2,'M',2,'e',2,'n',2,'u',0,' ',2,'0',2,'O',2,'o',2,'.',0,' ',0,' ',
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
7,'A',7,':',7,'a',7,'r',7,'c',7,'h',7,'e',7,'r',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',7,'F',7,':',7,'f',7,'i',7,'g',7,'h',7,'t',7,'e',7,'r',
7,'B',7,':',7,'a',7,'s',7,'s',7,'a',7,'s',7,'s',7,'i',7,'n',0,' ',0,' ',0,' ',0,' ',7,'G',7,':',7,'m',7,'i',7,'n',7,'e',7,'r',0,' ',0,' ',
7,'C',7,':',7,'c',7,'o',7,'m',7,'b',7,'a',7,'t',0,' ',7,'m',7,'a',7,'g',7,'e',0,' ',7,'H',7,':',7,'h',7,'e',7,'a',7,'l',7,'e',7,'r',0,' ',
7,'D',7,':',7,'m',7,'i',7,'n',7,'d',7,'c',7,'r',7,'a',7,'f',7,'t',7,'e',7,'r',0,' ',7,'I',7,':',7,'w',7,'i',7,'z',7,'a',7,'r',7,'d',0,' ',
7,'E',7,':',7,'s',7,'c',7,'o',7,'u',7,'t',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',7,'J',7,':',7,'t',7,'r',7,'a',7,'p',7,'p',7,'e',7,'r',
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
11,'T',11,':',0,' ',11,'s',11,'w',11,'i',11,'t',11,'c',11,'h',0,' ',11,'t',11,'e',11,'a',11,'m',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
0,' ',0,' ',
11,'S',11,':',0,' ',11,'s',11,'p',11,'e',11,'c',11,'t',11,'a',11,'t',11,'e',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
11,'L',11,':',0,' ',11,'l',11,'e',11,'a',11,'v',11,'e',0,' ',11,'t',11,'h',11,'e',0,' ',11,'l',11,'i',11,'m',11,'b',11,'o',0,' ',11,'m',
11,'e',11,'n',11,'u',
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',2,'.',2,'o',2,'O',2,'0',2,'.',2,'0',2,'O',2,'o',2,'.',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' '
};
/* This is how it looks without the colours:

 .oO0 Limbo Menu 0Oo.

A:archer      F:fighter
B:assassin    G:miner
C:combat mage H:healer
D:mindcrafter I:wizard
E:scout       J:trapper

T: switch team
S: spectate
L: leave the limbo menu

      .oO0.0Oo.
*/
}


void toggle_titles()
{
	if((showtitles = !showtitles))
		draw_titles();
	else
		redraw_view();
}


void redraw_view()
{
	if(clientstate == CS_LIMBO)
		Base::print_view(limboview);
	else
	{
		Base::print_view(viewbuffer);
		if(clientstate == CS_AIMING)
			draw_aimer();
		if(showtitles)
			draw_titles();
	}
}


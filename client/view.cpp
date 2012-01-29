// Please see LICENSE file.
#include "view.h"
#include "base.h"
#include "class_cpv.h"
#include "settings.h"
#include "../common/constants.h"
#include "../common/col_codes.h"
#include "../common/los_lookup.h"
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

const char aimerpath[] = "*";
const char aimerend[] = "X";

const unsigned char TITLE_CPAIR = C_BLUE_ON_MISS;

bool showtitles = false;
bool helpshown = false;

void draw_titles()
{
	char len;
	char *pos = &(viewbuffer[VIEWSIZE*VIEWSIZE*2+1]);
	char x, y;
	char syms;
	for(char num = viewbuffer[VIEWSIZE*VIEWSIZE*2]; num > 0; --num)
	{
		y = *(pos+1); // pos&pos+1 are x/y pair, string begins at pos+2.
		len = strlen(pos+2);
		// x-position might need shifting so the string doesn't go over the edge:
		syms = num_syms(string(pos+2));
		x = *pos - syms/2;
		if(x < 0)
			x = 0;
		else if(x + syms > VIEWSIZE)
			x = VIEWSIZE - syms;
		// If we are alive, don't draw any title coming to the center (that
		// would be our OWN title)
		if(*pos != VIEWSIZE/2 || y != VIEWSIZE/2 || !ClassCPV::im_alive())
			Base::print_str(pos+2, TITLE_CPAIR, x, y, VIEW_WIN);
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
0,' ',2,'.',2,'o',2,'O',2,'0',0,' ',2,'L',2,'i',2,'m',2,'b',2,'o',0,' ',2,'M',2,'e',2,'n',2,'u',0,' ',2,'0',2,'O',2,'o',2,'.',0,' ',0,' ',
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
7,'A',7,':',7,'a',7,'r',7,'c',7,'h',7,'e',7,'r',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',7,'F',7,':',7,'f',7,'i',7,'g',7,'h',7,'t',7,'e',7,'r',
7,'B',7,':',7,'a',7,'s',7,'s',7,'a',7,'s',7,'s',7,'i',7,'n',0,' ',0,' ',0,' ',0,' ',7,'G',7,':',7,'m',7,'i',7,'n',7,'e',7,'r',0,' ',0,' ',
7,'C',7,':',7,'c',7,'o',7,'m',7,'b',7,'a',7,'t',0,' ',7,'m',7,'a',7,'g',7,'e',0,' ',7,'H',7,':',7,'h',7,'e',7,'a',7,'l',7,'e',7,'r',0,' ',
7,'D',7,':',7,'m',7,'i',7,'n',7,'d',7,'c',7,'r',7,'a',7,'f',7,'t',7,'e',7,'r',0,' ',7,'I',7,':',7,'w',7,'i',7,'z',7,'a',7,'r',7,'d',0,' ',
7,'E',7,':',7,'s',7,'c',7,'o',7,'u',7,'t',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',7,'J',7,':',7,'t',7,'r',7,'a',7,'p',7,'p',7,'e',7,'r',
7,'K',7,':',7,'p',7,'l',7,'a',7,'n',7,'e',7,'w',7,'a',7,'l',7,'k',7,'e',7,'r',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
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
K:planewalker

T: switch team
S: spectate
L: leave the limbo menu

      .oO0.0Oo.
*/

// The help view similarly (not const because we change the keys!)
char helpview[VIEWSIZE*VIEWSIZE*2] = {
0,' ',2,'.',2,'o',2,'O',2,'0',0,' ',2,'K',2,'e',2,'y',2,'b',2,'i',2,'n',2,'d',2,'i',2,'n',2,'g',2,'s',0,' ',2,'0',2,'O',2,'o',2,'.',0,' ',
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',5,'7',5,' ',5,'8',5,' ',5,'9',5,' ',0,' ',3,'w',3,'m',3,'o',3,'d',3,'e',3,':',0,' ',5,'w',5,' ',0,' ',
3,'d',3,'i',3,'r',3,'s',3,':',0,' ',5,'4',5,' ',5,'5',5,' ',5,'6',5,' ',0,' ',3,'a',3,'x',3,'n',3,':',0,' ',0,' ',0,' ',5,' ',5,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',5,'1',5,' ',5,'2',5,' ',5,'3',5,' ',0,' ',3,'p',3,'r',3,'e',3,'v',3,':',0,' ',0,' ',5,'p',0,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',3,'c',3,'h',3,'a',3,'t',3,'/',3,'s',3,'a',3,'y',3,':',0,' ',5,'C',5,' ',3,'/',0,' ',5,'s',5,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
0,' ',0,' ',3,'q',3,'s',3,'h',3,'o',3,'u',3,'t',3,'s',3,':',0,' ',0,' ',5,'F',5,'1',5,'-',5,'F',5,'1',5,'2',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
0,' ',0,' ',3,'c',3,'.',3,' ',3,'d',3,'o',3,'o',3,'r',3,':',0,' ',0,' ',5,'c',5,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',3,'s',3,'u',3,'i',3,'c',3,'i',3,'d',3,'e',3,':',0,' ',0,' ',5,'X',5,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',3,'t',3,'r',3,'a',3,'p',3,' ',3,'t',3,'.',3,':',0,' ',0,' ',5,'T',5,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',3,'t',3,'o',3,'r',3,'c',3,'h',3,':',0,' ',0,' ',0,' ',0,' ',5,'u',5,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
3,'t',3,'i',3,'t',3,'l',3,'e',3,'s',3,':',0,' ',5,'t',5,' ',0,' ',0,' ',3,'\"',3,'o',3,'u',3,'c',3,'h',3,'\"',3,':',0,' ',5,'o',5,' ',0,' ',
3,'s',3,'e',3,'n',3,'d',3,'m',3,'o',3,'d',3,'e',3,' ',3,'t',3,'o',3,'g',3,'g',3,'l',3,'e',3,':',0,' ',0,' ',0,' ',0,' ',5,'%',5,' ',0,' ',
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',3,'c',3,'h',3,'a',3,'t',3,' ',3,'u',3,'/',3,'d',3,':',0,' ',5,'-',5,' ',3,'/',0,' ',5,'+',5,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
0,' ',0,' ',3,'l',3,'i',3,'m',3,'b',3,'o',3,':',0,' ',0,' ',0,' ',0,' ',5,'l',5,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',
0,' ',0,' ',3,'q',3,'u',3,'i',3,'t',3,':',0,' ',0,' ',0,' ',0,' ',0,' ',5,'Q',5,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
2,'(',2,'p',2,'r',2,'e',2,'s',2,'s',2,' ',2,'a',2,'n',2,'y',2,' ',2,'k',2,'e',2,'y',2,' ',2,'t',2,'o',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ', 
0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',0,' ',2,'c',2,'l',2,'o',2,'s',2,'e',2,' ',2,'t',2,'h',2,'i',2,'s',2,' ',2,'v',2,'i',2,'e',2,'w',2,')',
};
/* And this is how it looks without the colours (and with default keys):
 .oO0 Keybindings 0Oo.
      7 8 9  wmode: w  
dirs: 4 5 6  axn:      
      1 2 3  prev:  p          
                       
  chat/say: C / s       
  qshouts:  F1-F12      
                       
  c. door:  c          
  suicide:  X          
  trap t.:  T          
  torch:    u          
                       
titles: t   "ouch": o 
sendmode toggle:    % 
                       
  chat u/d: + / -        
  limbo:    l          
                       
  quit:     Q          
                       
(press any key to      
       close this view)
*/
// indices to modify the keys:
const short kmv_idx[MAX_KEY_BINDING] = {
	(1*VIEWSIZE + 8)*2+1, // 8
	(1*VIEWSIZE + 10)*2+1, // 9
	(2*VIEWSIZE + 10)*2+1, // 6
	(3*VIEWSIZE + 10)*2+1, // 3
	(3*VIEWSIZE + 8)*2+1, // 2
	(3*VIEWSIZE + 6)*2+1, // 1
	(2*VIEWSIZE + 6)*2+1, // 4
	(1*VIEWSIZE + 6)*2+1, // 7
	(2*VIEWSIZE + 8)*2+1, // 5
	(2*VIEWSIZE + 20)*2+1, // ' '
	(3*VIEWSIZE + 20)*2+1, // p
	(5*VIEWSIZE + 12)*2+1, // C
	(5*VIEWSIZE + 16)*2+1, // s
	(8*VIEWSIZE + 12)*2+1, // c
	(10*VIEWSIZE + 12)*2+1, // T
	(9*VIEWSIZE + 12)*2+1, // X
	(11*VIEWSIZE + 12)*2+1, // u
	(1*VIEWSIZE + 20)*2+1, // w
	(13*VIEWSIZE + 8)*2+1, // t
	(16*VIEWSIZE + 12)*2+1, // +
	(16*VIEWSIZE + 16)*2+1, // -
	(17*VIEWSIZE + 12)*2+1, // l
	(19*VIEWSIZE + 12)*2+1, // Q
	(13*VIEWSIZE + 20)*2+1, // o
	(14*VIEWSIZE + 20)*2+1 // %
};

} // end local namespace


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
	else if(clientstate == CS_HELP)
	{
		if(!helpshown) // first time to show help
		{
			// replace keys in the view with actual values
			char *kmv;
			short idx;
			for(e_Key_binding kb = KB_8; kb < MAX_KEY_BINDING; kb = e_Key_binding(kb+1))
			{
				kmv = Config::key_map_value(kb);
				idx = kmv_idx[kb];
				while(*kmv)
				{
					helpview[idx] = *kmv;
					idx += 2;
					++kmv;
				}
			}
			helpshown = true;
		}
		Base::print_view(helpview);
	}
	else
	{
		Base::print_view(viewbuffer);
		if(clientstate == CS_AIMING)
			draw_aimer();
		if(showtitles)
			draw_titles();
	}
}


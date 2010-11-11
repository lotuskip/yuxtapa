// Please see LICENSE file.
#include <ncurses.h>
#include <iostream>
#include "../client/colourdef.h"
#include "../common/col_codes.h"

short *term_colours;
enum e_ColorMode { CM_FULL=0, //full colours that can be changed
	CM_MANY, //full, immutable colours
	CM_FEW }; // 8+8 colour mode (xterm)
e_ColorMode colmode;

void change_colour(unsigned char cpair)
{
	if(colmode != CM_FEW)
		attrset(COLOR_PAIR(cpair));
	else
	{
		// Reduce the colours to the base 16:
		if(cpair >= BASE_COLOURS)
		{
			if(cpair < C_BG_HEAL)
				cpair = col_remap[cpair - BASE_COLOURS]; // (and goto set_colour)
			else // background is not black
			{
				// check the cases that require light foreground:
				if(cpair == C_BROWN_ON_TRAP || cpair == C_YELLOW_ON_TRAP
					|| cpair == C_GREEN_ON_POIS || cpair == C_RED_ON_HIT
					|| cpair == C_BLUE_ON_HEAL)
					attrset(A_BOLD
						| (COLOR_PAIR(cpair - C_BG_HEAL + BASE_COLOURS)));
				else
					attrset(COLOR_PAIR(cpair - C_BG_HEAL + BASE_COLOURS));
				return;
			}
		}
		// set_colour:
		if(cpair >= BASE_COLOURS/2)
			attrset((A_BOLD | (COLOR_PAIR(cpair))));
		else
			attrset(COLOR_PAIR(cpair));
	} // no fullcolourmode
}

const char ctestsym[] = "abcdefghijklmnop" // 16 base colours
"T..#+.~" // dims
"T..#+.~@@@^^^^^&AV/" // normals
"T..#+.~@@@^^^^^&AV/**t\\" // lits
"T@+#~^/T@+#~^/T@+#~^/T@+#~^/T@+#~^/T@+#~^/" // backgroundeds
"?"; // unknown

int main()
{
	initscr();
	start_color();
	short c; char d;
	char ct[BASE_COLOURS] = { COLOR_BLACK, COLOR_RED, COLOR_GREEN,
		COLOR_YELLOW, COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE,
		COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW, COLOR_BLUE,
		COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE };
	for(c = 0; c < BASE_COLOURS; ++c)
		init_pair(c, ct[c], COLOR_BLACK);

	if(can_change_color() && COLORS >= MAX_PREDEF_COLOUR)
	{
		colmode = CM_FULL;
		term_colours = new short[3*(MAX_PREDEF_COLOUR-BASE_COLOURS)];
		for(c = 0; c < MAX_PREDEF_COLOUR-BASE_COLOURS; ++c)
		{
			// store old colours:
			color_content(c+BASE_COLOURS, &term_colours[3*c],
				&term_colours[3*c+1], &term_colours[3*c+2]);
			// and replace with ours:
			init_color(c+BASE_COLOURS, colour[c][0], colour[c][1],
				colour[c][2]); 
		}
		// (see common/col_codes.h)
		for(c = BASE_COLOURS; c < C_BG_HEAL; ++c)
			init_pair(c, c, COLOR_BLACK);
		for(d = 0; d < 6; ++d)
		{
			for(c = 0; c < 7; ++c)
				init_pair(C_GREEN_ON_HEAL+d*7+c, C_BASE_GREEN+c, C_BG_HEAL+d);
		}
		init_pair(C_UNKNOWN, C_WALL_DIM, C_WALL_DIM); 
	}
	else if(COLORS >= MAX_PREDEF_COLOUR) // immutable multi-colour
	{
		colmode = CM_MANY;
		for(c = BASE_COLOURS; c < C_BG_HEAL; ++c)
			init_pair(c, fixed_remap[c-BASE_COLOURS], COLOR_BLACK);
		for(d = 0; d < 6; ++d)
		{
			for(c = 0; c < 7; ++c)
				init_pair(C_GREEN_ON_HEAL+d*7+c, fixed_fgc[c], fixed_bgc[d]);
		}
		init_pair(C_UNKNOWN, 8, 8);
	}
	else
	{
		colmode = CM_FEW;
		// Init the pairs with non-black background
		for(d = 0; d < 6; ++d)
		{
			for(c = 0; c < 7; ++c)
				init_pair(BASE_COLOURS+d*7+c, fgc[c], bgc[d]);
		}
		init_pair(BASE_COLOURS+6*7, BASE_LIGHT_GRAY, BASE_LIGHT_GRAY);
	}
	refresh();

	// Outside view.
	change_colour(C_UNKNOWN);
	addch('?');
	change_colour(C_TREE-NUM_DIM_COLS);
	addch('T');
	addch('\"');
	change_colour(C_GRASS-NUM_DIM_COLS);
	addch('.');
	change_colour(C_WATER-NUM_DIM_COLS);
	addch('~');
	change_colour(C_ROAD-NUM_DIM_COLS);
	addch('.');
	change_colour(C_TREE);
	addch('T');
	addch('\"');
	change_colour(C_GRASS);
	addch('.');
	change_colour(C_WATER);
	addch('~');
	change_colour(C_ROAD);
	addch('.');
	change_colour(C_TREE+NUM_NORM_COLS);
	addch('T');
	addch('\"');
	change_colour(C_GRASS+NUM_NORM_COLS);
	addch('.');
	change_colour(C_WATER+NUM_NORM_COLS);
	addch('~');
	change_colour(C_ROAD+NUM_NORM_COLS);
	addch('.');
	addch('\n');

	// Dungeon view:
	change_colour(C_UNKNOWN);
	addch('?');
	change_colour(C_WALL-NUM_DIM_COLS);
	addch('#');
	change_colour(C_FLOOR-NUM_DIM_COLS);
	addch('.');
	change_colour(C_DOOR-NUM_DIM_COLS);
	addch('\\');
	change_colour(C_WALL-NUM_DIM_COLS);
	addch('\"');
	change_colour(C_WALL);
	addch('#');
	change_colour(C_FLOOR);
	addch('.');
	change_colour(C_DOOR);
	addch('\\');
	change_colour(C_WALL);
	addch('\"');
	change_colour(C_WALL+NUM_NORM_COLS);
	addch('#');
	change_colour(C_FLOOR+NUM_NORM_COLS);
	addch('.');
	change_colour(C_DOOR+NUM_NORM_COLS);
	addch('\\');
	change_colour(C_WALL+NUM_NORM_COLS);
	addch('\"');
	addch('\n');

	// An Archer!
	change_colour(C_GREEN_PC);
	addch('@');
	change_colour(C_ARROW);
	addstr("**");
	change_colour(C_ARROW_LIT);
	addch('X');
	addch('\n');

	// ALL the colour pairs tested:
	for(c = 0; c < MAX_PREDEF_CPAIR; ++c)
	{
		change_colour(c);
		addch(ctestsym[c]);
	}
	addch('\n');

	refresh();
	getch();	

	if(colmode == CM_FULL)
	{
		// restore original colours: (this gives a nice shutdown "animation", too)
		for(c = 0; c < MAX_PREDEF_COLOUR-16; ++c)
			init_color(c+16, term_colours[3*c], term_colours[3*c+1], term_colours[3*c+2]);
		delete[] term_colours;
	}

	endwin();
	std::cout << "Colour mode: ";
	if(colmode == CM_FULL) std::cout << "full";
	else if(colmode == CM_MANY) std::cout << "many";
	else std::cout << "few";
	std::cout << std::endl;
	return 0;
}


//Please see LICENSE file.
#ifndef COL_CODES_H
#define COL_CODES_H

/* A word on the colour system.
 *
 * First, there are three "lighting conditions"; normal, dim, and lit.
 * Dim means things outside of the LOS. Normal means things that are
 * in the LOS but dark, and lit means things that are in the LOS
 * and lit. (Outside of LOS + lit is just outside of LOS -- you can't
 * see that it's lit!)
 *
 * Not anything can be dim, normal, or lit. Some things, e.g. torches,
 * are self-lighting, so they are always lit (or not drawn at all). Many
 * things are only drawn inside the LOS, so they're normal or lit. On the
 * other hand, some static things, such as trees, can indeed have any of
 * the three lightings.
 *
 * Then there is the issue of background colours. Normally the background
 * is black. yuxtapa uses a different background temporarily on individual
 * tiles to represent certain actions. Because there aren't enough colour
 * pairs to have ALL the possible combinations, we simplify the situation
 * when the background is non-black. Then, the front color is reduced to
 * a canonical colour (see the "base colours" below) that is closest to the
 * actual front colour.
 *
 * The colours are organised so that the transfers dim<-->normal<-->lit
 * can be accomplished by simply adding a constant to the colour index.
 * The reduction to a base colours has to be done in a less direct manner.
 *
 * Note that the server only knows about the colour keys (the below enum)
 * and to what entities they correspond to, while the client only knows
 * how to transform a colour index to an actual RGB triplet.
 *
 * Do make clear to yourself the distinction between "a colour" (RGB-triplet)
 * and a "color pair" (NCurses concept).
 *
 * ATM we need 62 colours and 91 colour pairs. The aim is to keep the
 * amounts within the limits of the 88-colour urxvt (88 colours, 256 pairs).
 * Moreover, we'd like to not touch the default colours (first 16), so that
 * means we've used up 62+16=78 colours and 91+16 = 107 pairs.
 *
 * Finally, in an environment that has only 16 colours (like xterm), we do a
 * simple reduction of colours: everything that has some shade of green
 * becomes green (not lit) or light green (lit), etc. Everything outside
 * of LOS is just gray. All this logic takes place only in the client end.
 *
 */
const char BASE_COLOURS = 16;

const char NUM_DIM_COLS = 7;
const char NUM_NORM_COLS = 20;

// These are the colours. Note that they begin after the 16 default colours
enum {
	C_TREE_DIM=BASE_COLOURS, C_GRASS_DIM, C_ROAD_DIM, C_WALL_DIM, C_DOOR_DIM,
	C_FLOOR_DIM, C_WATER_DIM, // to change these to normal, simply add NUM_DIM_COLS
	C_TREE, C_GRASS, C_ROAD, C_WALL, C_DOOR, C_FLOOR, C_WATER, // can be dimmed
	C_GREEN_PC, C_PURPLE_PC, C_BROWN_PC, C_WATER_TRAP, C_LIGHT_TRAP,
	C_TELE_TRAP, C_BOOBY_TRAP, C_FIREB_TRAP, C_ACID_TRAP,
	C_NEUT_FLAG, C_PORTAL_IN,
	C_PORTAL_OUT, C_ARROW, // to upgrade to lit, add NUM_NORM_COLS
	C_TREE_LIT, C_GRASS_LIT, C_ROAD_LIT, C_WALL_LIT, C_DOOR_LIT,
	C_FLOOR_LIT, C_WATER_LIT, // these have dims, too
	C_GREEN_PC_LIT, C_PURPLE_PC_LIT, C_BROWN_PC_LIT, C_WATER_TRAP_LIT,
	C_LIGHT_TRAP_LIT, C_TELE_TRAP_LIT, C_BOOBY_TRAP_LIT, C_FIREB_TRAP_LIT,
	C_ACID_TRAP_LIT,
	C_NEUT_FLAG_LIT, C_PORTAL_IN_LIT, C_PORTAL_OUT_LIT, C_ARROW_LIT,
	// the rest can only be lit (since they *light*!)
	C_MM1, C_MM2, C_TORCH, C_ZAP,
	// next, the background indicator colours:
	C_BG_HEAL, C_BG_TRAP, C_BG_POISON, C_BG_DISGUISE, C_BG_HIT, C_BG_MISS,
	// base colours on the backgrounds:
	C_BASE_GREEN, // <- tree, grass, greenPC, tele_trap
	C_BASE_PURPLE, // <- purplePC, mm
	C_BASE_BROWN, // <- road, door, brownPC, arrow
	C_BASE_GRAY, // <- wall, floor, boobytrap, neut_flag, portal_out
	C_BASE_BLUE, // <- water, portal_in, water_trap
	C_BASE_RED, // <- fireb_trap
	C_BASE_YELLOW, // <- light_trap, torch, zap
	MAX_PREDEF_COLOUR };

// These are the color pairs; they overlap partially with the colours, since
// we identify "non-base-color X" == "X on black background".
enum {
	C_GREEN_ON_HEAL = C_BG_HEAL,
	C_PURPLE_ON_HEAL, C_BROWN_ON_HEAL, C_GRAY_ON_HEAL, C_BLUE_ON_HEAL,
	C_RED_ON_HEAL, C_YELLOW_ON_HEAL, // heals
	C_GREEN_ON_TRAP, C_PURPLE_ON_TRAP, C_BROWN_ON_TRAP, C_GRAY_ON_TRAP,
	C_BLUE_ON_TRAP, C_RED_ON_TRAP, C_YELLOW_ON_TRAP, // traps
	C_GREEN_ON_POIS, C_PURPLE_ON_POIS, C_BROWN_ON_POIS, C_GRAY_ON_POIS,
	C_BLUE_ON_POIS, C_RED_ON_POIS, C_YELLOW_ON_POIS, // poisons
	C_GREEN_ON_DISG, C_PURPLE_ON_DISG, C_BROWN_ON_DISG, C_GRAY_ON_DISG,
	C_BLUE_ON_DISG, C_RED_ON_DISG, C_YELLOW_ON_DISG, // disguises
	C_GREEN_ON_HIT, C_PURPLE_ON_HIT, C_BROWN_ON_HIT, C_GRAY_ON_HIT,
	C_BLUE_ON_HIT, C_RED_ON_HIT, C_YELLOW_ON_HIT, // hits
	C_GREEN_ON_MISS, C_PURPLE_ON_MISS, C_BROWN_ON_MISS, C_GRAY_ON_MISS,
	C_BLUE_ON_MISS, C_RED_ON_MISS, C_YELLOW_ON_MISS, // misses
	C_UNKNOWN, // unexplored area or outside of map
	MAX_PREDEF_CPAIR };

// cf. classes_common.h
const unsigned char team_colour[] = { C_GREEN_PC, C_PURPLE_PC, C_NEUT_FLAG };

#endif

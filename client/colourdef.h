// Please see LICENSE file.
#ifndef COLOUR_DEF_H
#define COLOUR_DEF_H

#include "../common/col_codes.h"

// "FULL" mode:
// colours are defined in NCurses system, RGB values 0-1000
const short colour[MAX_PREDEF_COLOUR-BASE_COLOURS][3] = {
{ 184, 215, 148 }, //  tree, dim
{ 320, 422, 297 }, //  grass, dim
{ 316, 262, 223 }, //  road, dim
{ 180, 180, 180 }, //  wall, dim
{ 215, 180, 148 }, //  door, dim
{ 217, 265, 214 }, //  floor, dim
{ 258, 305, 367 }, //  water, dim
// end of dims, normals:
{ 197, 331, 52 }, //  tree
{ 271, 667, 180 }, //   grass
{ 491, 243, 67 }, //  road
{ 282, 282, 282 }, //  wall
{ 338, 180, 59 }, //  door
{ 334, 412, 331 }, //  floor
{ 371, 399, 575 }, //  water, last that dims
{ 270, 878, 0 }, //  greenPC
{ 521, 125, 458 }, //  purplePC
{ 580, 525, 70 }, //  brownPC
{ 160, 11, 796 }, //  watertrap
{ 831, 749, 47 }, //  lighttrap
{ 43, 701, 243 }, //  teletrap
{ 509, 376, 376 }, //  boobytap
{ 831, 215, 0 }, //  firebtrap
{ 240, 550, 340 }, //  acidtrap
{ 705, 768, 784 }, //  neutrlflag
{ 258, 592, 690 }, //  portal_in
{ 403, 482, 501 }, //  portal_out
{ 521, 368, 258 }, //  arrow
// end of normals, lits:
{ 288, 483, 77 }, //  tree, lit
{ 396, 979, 263 }, //  grass, lit
{ 724, 362, 99 }, //  road, lit
{ 405, 405, 405 }, //  wall, lit
{ 504, 267, 90 }, //  door, lit
{ 492, 604, 487 }, //  floor, lit
{ 400, 586, 845 }, //  water, lit
{ 305, 1000, 0 }, //  greenPC, lit
{ 619, 78, 611 }, //  purplePC, lit
{ 698, 635, 86 }, //  brownPC, lit
{ 192, 15, 960 }, //  watertrap, lit
{ 1000, 901, 58 }, //  lighttrap, lit
{ 50, 839, 298 }, //  teletrap, lit
{ 607, 450, 450 }, //  boobytrap, lit
{ 1000, 258, 0 }, //  firebtrap, lit
{ 324, 746, 461 }, //  acidtrap, lit
{ 847, 917, 941 }, //  neutrlflag, lit
{ 309, 713, 827 }, //  portal_in, lit
{ 482, 572, 600 }, //  portal_out, lit
{ 619, 439, 305 }, //  arrow, lit, last that can be normal
{ 910, 90, 510 }, //  mm1
{ 945, 133, 360 }, //  mm2
{ 952, 643, 47 }, //  torch
{ 917, 1000, 0 }, //  zap
// end of lits, backgrounds:
{ 117, 517, 890 }, //  bgheal
{ 890, 505, 117 }, //  bgtrap
{ 427, 890, 117 }, //  bgpoison
{ 906, 914, 438 }, //  bgdisguise
{ 890, 117, 117 }, //  bghit
{ 627, 627, 627 }, //  bgmiss
// end of backgrounds, bases on backgrounds:
{ 141, 533, 101 }, //  basegreen
{ 533, 101, 411 }, //  basepurple
{ 533, 305, 101 }, //  basebrown
{ 321, 321, 321 }, //  basegray
{ 101, 282, 533 }, //  baseblue
{ 533, 101, 101 }, //  basered
{ 820, 762, 129 } //  baseyellow
};


// "MANY" mode; at least 88 colours but cannot redefine them.
// We assume the "standard 256 xterm colours", and make the best of them.
const unsigned char fixed_remap[C_BG_HEAL-BASE_COLOURS] = {
	// the dims:
	238, 238, 238, 238, 238, 238, 238,
	34, // tree
	112, // grass
	136, // road
	8, // wall
	172, // door
	245, // floor
	12, // water
	10, // green pc
	91, // purple pc
	172, // brown pc
	33, // water trap
	220, // light trap
	76, // tele trap
	241, // booby trap
	160, // fireb trap
	35, // acid trap
	7, // neutral flag
	6, // portal in
	8, // portal out
	178, // arrow
	// lits:
	40, // tree
	118, // grass
	214, // road
	245, // wall
	208, // door
	250, // floor
	32, // water
	46, // green pc
	128, // purple pc
	214, // brown pc
	69, // water trap
	226, // light trap
	118, // tele trap
	250, // booby trap
	196, // fireb trap
	41, // acid trap
	15, // neutral flag
	14, // portal in
	245, // portal out
	220, // arrow
	// only lits:
	126, 135, // mm
	166, // torch
	11 // zap
};

const unsigned char fixed_bgc[6] = { 4, 3, 2, 6, 1, 240 };
const unsigned char fixed_fgc[7] = { 10, 91, 11, 16, 12, 9, 11 };

// "FEW" mode (8+8 colours):
//
enum { BASE_BLACK=0, BASE_RED, BASE_GREEN, BASE_BROWN, BASE_BLUE,
	BASE_MAGENTA, BASE_CYAN, BASE_LIGHT_GRAY,
	// the rest need A_BOLD attribute:
	BASE_DARK_GRAY, BASE_LIGHT_RED, BASE_LIGHT_GREEN, BASE_YELLOW,
	BASE_LIGHT_BLUE, BASE_LIGHT_MAGENTA, BASE_LIGHT_CYAN, BASE_WHITE };

// For non-black backgrounds
const char bgc[6] = { BASE_BLUE, BASE_BROWN, BASE_GREEN, BASE_CYAN,
	BASE_RED, BASE_LIGHT_GRAY };
const char fgc[7] = { BASE_GREEN, BASE_MAGENTA, BASE_BROWN, BASE_BLACK,
	BASE_BLUE, BASE_RED, BASE_BROWN };

// Remapping of the colours in ../common/col_codes.h for 16 colours:
const char col_remap[C_BG_HEAL-BASE_COLOURS] = {
	// the dims:
	BASE_DARK_GRAY, BASE_DARK_GRAY, BASE_DARK_GRAY, BASE_DARK_GRAY,
	BASE_DARK_GRAY, BASE_DARK_GRAY, BASE_DARK_GRAY,
	BASE_GREEN, // tree
	BASE_GREEN, // grass
	BASE_BROWN, // road
	BASE_LIGHT_GRAY, // wall
	BASE_BROWN, // door
	BASE_LIGHT_GRAY, // floor
	BASE_BLUE, // water
	BASE_CYAN, // green pc
	BASE_MAGENTA, // purple pc
	BASE_BROWN, // brown pc
	BASE_BLUE, // water trap
	BASE_YELLOW, // light trap (NOTE: same as lit!)
	BASE_GREEN, // tele trap
	BASE_LIGHT_GRAY, // booby trap
	BASE_RED, // fireb trap
	BASE_CYAN, // acid trap
	BASE_LIGHT_GRAY, // neutral flag
	BASE_BLUE, // portal in
	BASE_LIGHT_GRAY, // portal out
	BASE_BROWN, // arrow
	// lits:
	BASE_LIGHT_GREEN, // tree
	BASE_LIGHT_GREEN, // grass
	BASE_YELLOW, // road
	BASE_WHITE, // wall
	BASE_YELLOW, // door
	BASE_WHITE, // floor
	BASE_LIGHT_BLUE, // water
	BASE_LIGHT_CYAN, // green pc
	BASE_LIGHT_MAGENTA, // purple pc
	BASE_YELLOW, // brown pc
	BASE_LIGHT_BLUE, // water trap
	BASE_YELLOW, // light trap
	BASE_LIGHT_GREEN, // tele trap
	BASE_WHITE, // booby trap
	BASE_LIGHT_RED, // fireb trap
	BASE_LIGHT_CYAN, // acid trap
	BASE_WHITE, // neutral flag
	BASE_LIGHT_BLUE, // portal in
	BASE_WHITE, // portal out
	BASE_BROWN, // arrow (NOTE: same as non-lit to distinguish from a zap)
	// only lits:
	BASE_MAGENTA, BASE_LIGHT_MAGENTA, // mm
	BASE_YELLOW, // torch
	BASE_YELLOW // zap
};


#endif

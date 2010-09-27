// Please see LICENSE file.
#ifndef SPIRAL_H
#define SPIRAL_H

#include "../common/coords.h"

// The spiral algorithm for placing entities in a neighbourhood of a given point:
// Given a point 'X', this algorithm will return in a sequence the points
// "circling" X, starting from the close ones and moving further:
//
//          ^
//         19  6> 7> 8> 9
//          ^  ^        v
//         18  5  X> 1 10
//          ^  ^     v  v
//         17  4 <3 <2 11
//          ^           v
//         16<15<14<13<12
//
// The algorithm also knows how to avoid the edges of the map, basically ignoring
// any points that would land outside the edge.

// Usage: First, give the map dimension if it has changed.
// Then, give center coordinates (X) with init_nearby, and then call
// next_nearby as many times as needed.
void nearby_set_dim(const short mapsize);
void init_nearby(const Coords &c);
Coords next_nearby();

#endif

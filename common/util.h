//Please see LICENSE file.
#ifndef UTIL_H
#define UTIL_H

// randor0(N) is the equivalent of random()%N, but works with N==1, too (it just
// returns 0 in the case N==1).
short randor0(const short size);

#endif

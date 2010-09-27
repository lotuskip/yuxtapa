//Please see LICENSE file.
#ifndef UTIL_H
#define UTIL_H

// When we want a random number between 0 and N-1, we usually do
// random()%N. But if N is equal to 0 we want to get 0, and for that
// purpose use this:
short randor0(const short size);

#endif

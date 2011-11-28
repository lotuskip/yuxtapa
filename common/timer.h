//Please see LICENSE file.
#ifndef TIMER_H
#define TIMER_H

#include <sys/time.h>

// This is essentially a wrapper for the gettimeofday()-interface.
// What we need is millisecond accuracy and an easy way to tell the
// difference (in ms) between two times.
class msTimer
{
public:
	msTimer &update(); // updates from system clock and returns *this
	unsigned int operator-(msTimer &rhs) const; // computes difference in ms

	time_t secs() const { return tv.tv_sec; }
	suseconds_t usecs() const { return tv.tv_usec; }
private:
	timeval tv;	
};

#endif

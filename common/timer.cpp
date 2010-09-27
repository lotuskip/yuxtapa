//Please see LICENSE file.
#include "timer.h"

msTimer &msTimer::update()
{
	gettimeofday(&tv, 0);
	return *this;
}

unsigned int msTimer::operator-(msTimer &rhs) const
{
	return (tv.tv_sec - rhs.secs())*1000 + (tv.tv_usec - rhs.usecs())/1000;
}

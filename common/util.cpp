//Please see LICENSE file.

#include <cstdlib>

short randor0(const short size)
{
	if(size == 1)
		return 0;
	return random()%size;
}


//Please see LICENSE file.
#include "util.h"
#include <cstdlib>
#include <sstream>

using namespace std;

short randor0(const short size)
{
	if(size == 1)
		return 0;
	return random()%size;
}


void trim_str(string &s)
{
	while(!s.empty() && s[0] == ' ')
		s.erase(0,1);
	while(!s.empty() && s[s.size()-1] == ' ')
		s.erase(s.size()-1);
}


string lex_cast(const int n)
{
	stringstream ss;
	ss << n;
	return ss.str();
}

string lex_cast_fl(const float f)
{
	stringstream ss;
	ss << f;
	return ss.str();
}

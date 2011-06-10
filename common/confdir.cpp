//Please see LICENSE file.
#include "confdir.h"
#include <cstdlib>

std::string get_conf_dir()
{
	std::string s;
	char *tmp = getenv("XDG_CONFIG_HOME"); // not all users have set this
	if(!tmp)
	{
		s = getenv("HOME"); // this should be set, though
		s += "/.config";
	}
	else
		s = std::string(tmp);
	s += "/yuxtapa/";
	return s;
}

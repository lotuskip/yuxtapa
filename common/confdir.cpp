//Please see LICENSE file.
#include "confdir.h"
#include <cstdlib>
#include <iostream>

std::string get_conf_dir()
{
	std::string s;
	char *tmp = getenv("XDG_CONFIG_HOME"); // not all users have set this
	if(tmp)
		s = std::string(tmp);
	else
	{
		if(!(tmp = getenv("HOME"))) // this should be set, though
		{
			std::cerr << "Neither HOME nor XDG_CONFIG_HOME defined!" << std::endl;
			exit(1);
		}
		s += "/.config";
	}
	s += "/yuxtapa/";
	return s;
}

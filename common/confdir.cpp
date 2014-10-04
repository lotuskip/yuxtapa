//Please see LICENSE file.
#include "confdir.h"
#include <cstdlib>
#include <iostream>

std::string get_conf_dir()
{
	std::string s;
	char *tmp = getenv("HOME");
	if(tmp && tmp[0])
		s = std::string(tmp);
	else
	{
		std::cerr << "HOME not defined!" << std::endl;
		exit(1);
	}
	s += "/.config/yuxtapa/";
	return s;
}

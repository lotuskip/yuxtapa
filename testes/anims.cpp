// Please see LICENSE file.
#include <ncurses.h>
#include <ctime>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "../common/utfstr.h"

using namespace std;

void usage()
{
	cout << "Usage: ./anims [animation, 2-15 symbols] [turnms, default 250, min 10]" << endl;
}


int main(int argc, char *argv[])
{
	if(argc < 2)
	{
		usage();
		return 1;
	}
	string anim = argv[1];
	vector<string> syms;
	size_t i, j = 0;
	for(i = 0; i < anim.size(); ++i)
	{
		if(static_cast<unsigned char>(anim[i]) < 0x80 || static_cast<unsigned char>(anim[i]) > 0xBF)
		{
			if(i != 0)
			{
				syms.push_back(anim.substr(j, i-j));
				j = i;
			}
		}
	}
	syms.push_back(anim.substr(j));

	if(syms.size() < 2 || syms.size() > 15)
	{
		usage();
		return 2;
	}
	
	int turnms = 250;
	if(argc >= 3 && (turnms = atoi(argv[2])) < 10)
	{
		usage();
		return 3;
	}

	initscr();
	noecho();
	timeout(turnms);
	
	j = 0;
	do {
		move(0,0);
		addstr(syms[j].c_str());
		refresh();
		if(++j == syms.size())
			j = 0;
	} while(getch() != 'q');

	endwin();
	return 0;
}


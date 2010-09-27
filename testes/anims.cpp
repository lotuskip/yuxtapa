// Please see LICENSE file.
#include <ncurses.h>
#include <ctime>

const char anim[10][5] =
{ ".oOo", "-\\|/", "LFEF", "v<v>", "])})", "pbdq", "-~=~", "+x+x", "sS$S", "DB8B" };

int main()
{
	initscr();

	noecho();
	timeout(250);
	
	char i, j = 0;
	while(getch() != 'q')
	{
		for(i = 0; i < 10; ++i)
		{
			move(2*i,0);
			addch(anim[i][j]);
		}
		refresh();
		if(++j == 4)
			j = 0;
	}

	endwin();
	return 0;
}


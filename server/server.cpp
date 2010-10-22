// Please see LICENSE file.
#include "network.h"
#include "settings.h"
#include "players.h"
#include "sighandle.h"
#include "log.h"
#include "game.h"
#include "../common/timer.h"
#include <ctime>
#include <unistd.h>
#include <string>
#include <iostream>
#include <boost/lexical_cast.hpp>

bool intermission = true;

namespace
{
short turns_lagged;
time_t last_time, interm_over;
msTimer last_clock;

using std::string;
const string print_stats_handle = "--pstats";

void goto_next_map()
{
	intermission = false;
	turns_lagged = 0;
	Game::start();
	last_clock.update();
}

void goto_intermission()
{
	intermission = true;
	Game::next_map();
	if(turns_lagged)
	{
		string str = "Warning: the server lagged ";
		str += boost::lexical_cast<string>(turns_lagged);
		str += " turns this map.";
		to_log(str);
	}
	interm_over = time(NULL) + Config::int_settings[Config::IS_INTERM_SECS];
	Network::clocksync(interm_over - time(NULL), 0);
}


} // end local namespace

void next_map_forced()
{
	if(intermission)
		goto_next_map();
	else
		goto_intermission();
}


#ifdef MAPTEST
#include "map.h"
#endif
int main(int argc, char *argv[])
{
#ifdef MAPTEST
	unsigned short size;
	if(argc < 2 || (size = atoi(argv[1])) < 42 || size > 511)
	{
		std::cout << "Try: ./yuxtapa_sv [mapsize, 42--511]" << std::endl;
		return 1;
	}
	Map a_map(size, 0, 10); // will generate and print the map	
#else
	string str = "";
	if(argc >= 2)
		str = argv[1];
	
	if(str == "--help" || str == "-h" || str == "-v" || str == "--version")
	{
		std::cout << PACKAGE << " server v. " << VERSION << std::endl;
		return 0;
	}

	using namespace Config;
	read_config();
	init_known_players(str == print_stats_handle);

	if(str == print_stats_handle)
	{
		output_stats();
		return 0;
	}
	/*else*/if(!str.empty())
		std::cout << "Uknown argument \"" << str << '\"' << std::endl;

	SIGINT_Handler sigint_handler;
	SIGQUIT_Handler sigquit_handler;
	Signal_Handler::instance()->register_handler(SIGINT, &sigint_handler);
	Signal_Handler::instance()->register_handler(SIGQUIT, &sigquit_handler);

	if(!Network::startup())
		return 1;

	srandom(time(NULL));

	msTimer test_clock;
	time(&last_time);
	time_t last_minute = time(NULL);
	interm_over = last_time + int_settings[IS_INTERM_SECS];
	tm *loctime;
	bool no_traffic;
	while(!sigint_handler.graceful_quit() && !sigquit_handler.abortive_quit())
	{
		no_traffic = Network::receive_n_handle();

		if(intermission)
		{
			Game::init_game();
			if(time(NULL) >= interm_over) // time to end intermission
				goto_next_map();
			else if(no_traffic)
				usleep(500000); // half a second
		}
		else // not intermission
		{
			// high-res (ms-scale) time checks: check if a turn has passed
			if(test_clock.update() - last_clock > int_settings[IS_TURNMS])
			{
				// Lag check:
				turns_lagged += (test_clock - last_clock)/int_settings[IS_TURNMS]-1;
				last_clock.update();
				if(Game::process_turn())
					goto_intermission();
			}
			else if(no_traffic) // micro-hibernation
				usleep(int_settings[IS_TURNMS]*10); // sleep for 1/100th of a turn (2.5ms on normal config)
		}

		// low-res (second scale) time checks:
		if(time(NULL) - last_time > 5) // 5 seconds have passed
		{
			last_time = time(NULL);
			// Send clock update every 5 seconds
			if(intermission)
				Network::clocksync(interm_over - time(NULL), 0);
			else
				Game::send_times(cur_players.end());
		}
		if(time(NULL) - last_minute > 60) // a minute has passed
		{
			last_minute = time(NULL);
			loctime = localtime(&last_minute);
			if(!(loctime->tm_min % 30)) // usage check every 30 minutes
			{
				if(check_date_change(loctime->tm_mday)) // the day has changed
					usage_report();
				else
					usage_update();
			}
		} // minute passed
	} // for eva

	Network::shutdown();
	timed_log("Shutdown");
	usage_report();
	to_log("---------------------------------");
	store_known_players();
#endif
	return 0;
}

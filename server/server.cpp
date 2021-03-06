// Please see LICENSE file.
#include <iostream>
#include <cstdlib>

#ifdef MAPTEST
#include "map.h"
#else
#include "network.h"
#include "settings.h"
#include "sighandle.h"
#include "log.h"
#include "game.h"
#include "../common/timer.h"
#include "../common/util.h"
#include <ctime>
#include <unistd.h>

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

void goto_intermission(const string &loadmap)
{
	intermission = true;
	Game::next_map(loadmap);
	if(turns_lagged)
	{
		string str = "Warning: the server lagged "
			+ lex_cast(turns_lagged) + " turns this map.";
		to_log(str);
	}
	interm_over = time(NULL) + Config::int_settings[Config::IS_INTERM_SECS];
	Network::clocksync(interm_over - time(NULL), 0);
}


} // end local namespace

void next_map_forced(const string &loadmap)
{
	if(intermission && loadmap.empty())
		goto_next_map();
	else
		goto_intermission(loadmap);
}

#endif // not maptest build


int main(int argc, char *argv[])
{
#ifdef MAPTEST
	unsigned short size;
	if(argc < 2 || (size = atoi(argv[1])) < MIN_MAP_SIZE || size > MAX_MAP_SIZE)
	{
		std::cout << "Try: ./yuxtapa_sv [mapsize, " << MIN_MAP_SIZE
			<< "--" << MAX_MAP_SIZE << ']' << std::endl;
		std::cout << "Third argument, if present, is a filename to save the map as "
			"(it will be saved in the working directory)." << std::endl;
		return 1;
	}
	srandom(time(NULL));
	Map a_map(size, 0, 10); // will generate and print the map
	if(argc > 2)
	{
		if(!a_map.save_to_file(argv[2]))
			std::cout << "Failed to save the map as \'" << argv[2] << "\'!" << std::endl;
	}
#else
	setlocale(LC_ALL, "");
	string str = "";
	if(argc >= 2)
		str = argv[1];
	
	if(str == "--help" || str == "-h")
	{
		std::cout << "Recognised options are: \'-v\' (print version) and \'"
			<< print_stats_handle << "\' (print player stats)" << std::endl;
		return 0;
	}
	if(str == "-v" || str == "--version")
	{
		std::cout << PACKAGE << " server v." << VERSION << std::endl;
		return 0;
	}

	using namespace Config;
	if(str == print_stats_handle)
	{
		init_known_players(true);
		output_stats();
		return 0;
	}
	// else going to start the server for real
	std::cout << "(__)ux+apa " << VERSION << std::endl;
	std::cout << " ._)" << std::endl << std::endl;
	if(!str.empty())
		std::cout << "Uknown argument \"" << str << '\"' << std::endl;
	reg_sig_handlers();
	read_config();
	init_known_players(false);

	if(!Network::startup())
		return 1;

	srandom(time(NULL));

	msTimer test_clock;
	time(&last_time);
	time_t last_minute = time(NULL);
	interm_over = last_time + int_settings[IS_INTERM_SECS];
	tm *loctime;
	bool no_traffic;
	while(!any_signs())
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
					goto_intermission("");
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

	std::cout << "Quitting..." << std::endl;
	Network::shutdown();
	timed_log("Shutdown");
	usage_report();
	to_log("---------------------------------");
	store_known_players();
#endif
	return 0;
}

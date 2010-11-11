// Please see LICENSE file.
#ifndef MAPTEST
#include "log.h"
#include "settings.h"
#include <fstream>
#include <iostream>
#include <ctime>

namespace
{
using namespace std;

const string logfilename = "server.log";

bool canlog = true;

int cur_mday = -1; // current day of month; to check whether the date has changed
}


void to_log(const string &s)
{
	if(canlog)
	{
		ofstream logfile((Config::get_config_dir() + logfilename).c_str(),
			ios_base::app);
		if(!logfile)
		{
			cerr << "Could not open log file!" << endl;
			canlog = false;
			return;
		}
		logfile << s << endl;
	}
#ifdef DEBUG
	cerr << "LOG: " << s << endl;
#endif
}


void timed_log(const string &s)
{
	time_t rawtime;
	struct tm *timeinfo;
	char buffer[7]; // eg. "12:13 " (and '\0')

	time(&rawtime);
 	timeinfo = localtime(&rawtime);
	strftime(buffer, 7, "%H:%M ", timeinfo);
	to_log(string(buffer) + s);
}


bool check_date_change(const int d)
{
	// This check is only true on the first test, when the day is not yet
	// initialized:
	if(cur_mday == -1)
		cur_mday = d;
	else if(cur_mday != d)
	{
		time_t rawt;
		char b[30];
		time(&rawt);
		tm *timeinfo = localtime(&rawt);
		strftime(b, 30, "Day changed to %a %d %b", timeinfo);
		to_log(string(b));
		cur_mday = d;
		return true;
	}
	return false;
}

#endif // not maptest build

// Please see LICENSE file.
#ifndef MAPTEST
#include "sighandle.h"
#include <csignal>

sig_atomic_t signs = 0;

void handle_signal(const int signum)
{
	signs |= (1 << signum);
}


bool any_signs() { return signs; }

void reg_sig_handlers()
{
	struct sigaction sa;
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	// List here all signals we're interested in:
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGQUIT, &sa, 0);
}

#endif // not maptest build

// Please see LICENSE file.
#ifndef MAPTEST
#include "sighandle.h"

Signal_Handler *Signal_Handler::instance_ = 0;
Event_Handler *Signal_Handler::signal_handlers_[NSIG];

Signal_Handler::Signal_Handler() {}

Signal_Handler* Signal_Handler::instance() 
{
	if(!instance_)
		instance_ = new Signal_Handler();
	return instance_;
}


Event_Handler *Signal_Handler::register_handler(const int signum,
	Event_Handler *eh)
{
	// Copy the old_eh from the signum slot in the signal_handlers_ table.
	Event_Handler *old_eh = signal_handlers_[signum];
	signal_handlers_[signum] = eh;

	// Register the <dispatcher> to handle this <signum>.
	struct sigaction sa;
	sa.sa_handler = dispatcher;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(signum, &sa, 0);

	return old_eh;
}

void Signal_Handler::dispatcher(const int signum)
{
	if(signal_handlers_[signum] != 0) // Dispatch the handler's hook method.
		signal_handlers_[signum]->handle_signal(signum);
}
 
#endif // not maptest build

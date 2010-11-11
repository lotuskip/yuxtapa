// Please see LICENSE file.
#ifndef SIGHANDLE_H
#define SIGHANDLE_H

#include <csignal>

class Event_Handler
{
public:
	// Hook method for the signal hook method.
	virtual void handle_signal(const int signum) = 0;
};


class Signal_Handler
{
public:
	static Signal_Handler *instance(); // Entry point.

	// Register an event handler eh for signum and return a pointer to any
	// existing Event_Handler that was previously registered to handle signum.
	Event_Handler *register_handler(const int signum, Event_Handler* const eh);

	int remove_handler(const int signum);

private:
	Signal_Handler(); // Ensure we're a Singleton.

	static Signal_Handler *instance_;

	// Entry point adapter installed into sigaction (must be a static
	// method or a stand-alone extern "C" function).
	static void dispatcher(const int signum);

	// Table of pointers to concrete Event_Handlers registered by applications.
	// NSIG is the number of signals defined in </usr/include/sys/signal.h>.
	static Event_Handler *signal_handlers_[NSIG];
};


class SIGINT_Handler : public Event_Handler
{
public:
	SIGINT_Handler() : graceful_quit_(0) {}

	virtual void handle_signal(const int signum)
		{ this->graceful_quit_ = 1; }

	sig_atomic_t graceful_quit() { return this->graceful_quit_; }

private:
	sig_atomic_t graceful_quit_;
};


class SIGQUIT_Handler : public Event_Handler
{
public:
	SIGQUIT_Handler() : abortive_quit_ (0) {}

	virtual void handle_signal(const int signum)
		{ this->abortive_quit_ = 1; }

	sig_atomic_t abortive_quit() { return this->abortive_quit_; }

private:
	sig_atomic_t abortive_quit_;
};

#endif

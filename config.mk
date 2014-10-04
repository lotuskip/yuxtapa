# Modify below to fit your needs.

#This option for a pedantic, warnful debug build:
CONFIG_FLAGS=-ggdb -O0 -Wall -Wextra -pedantic -Wno-char-subscripts -DDEBUG
#
#This one for the optimised version:
#CONFIG_FLAGS=-O2 -g0

# Define your C++ compiler.
#CXX=clang++
CXX=g++

EXTRA_FLAGS=
# Add -DSIMPLE_CURSES_HEADER if you get errors about "ncursesw/ncurses.h"
# not being found
# Add -DBOT_IPV6 to have the bots connect to localhost using IPv6 instead of v4
# Add -DMAPTEST to build a "server" for testing & creating maps
# Add -DBOTMSG to make the bot client print messages
# Add -DTIME_SERVER_TURN to time the server render (prints a lot of numbers!)
# Adding -D_XOPEN_SOURCE_EXTENDED on OSX seems to be necessary (for the client)

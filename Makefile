# yuxtapa Makefile
VERSION = 9
#
#This option for a pedantic, warnful debug build:
#CONFIG_FLAGS=-ggdb -O0 -Wall -pedantic -Wno-char-subscripts -DDEBUG
#
#This one for the optimised version:
CONFIG_FLAGS=-O2 -g0

EXTRA_FLAGS=
# Add -DBOT_IPV6 to have the bots connect to localhost using IPv6 instead of v4
# Add -DMAPTEST to build a "server" for testing & creating maps.
# Add -DBOTMSG to make the bot client print messages.
# Add -DTIME_SERVER_TURN to time the server render (prints a lot of numbers!)
# Add -static to build statically linked binaries.
# Adding -D_XOPEN_SOURCE_EXTENDED on OSX seems to be necessary (for the client)

#The rest probably needs not be touched.
###############################################################################
CPPFLAGS=$(CONFIG_FLAGS) $(EXTRA_FLAGS) -fsigned-char -DVERSION=$(VERSION)
CXX=g++
RM=rm -f
LDLIBS_CL=-lncursesw -lz
LDLIBS_SV=-lz
LDLIBS_MB=-lz

SRCS_CO = common/timer.cpp common/netutils.cpp common/utfstr.cpp \
	common/confdir.cpp common/coords.cpp common/util.cpp
OBJS_CO=$(subst .cpp,.o,$(SRCS_CO))

SRCS_CL = client/base.cpp client/class_cpv.cpp client/client.cpp \
	client/input.cpp client/msg.cpp client/network.cpp \
	client/settings.cpp client/view.cpp
OBJS_CL=$(subst .cpp,.o,$(SRCS_CL))

OBJS_MB=testes/mrbrown.o common/netutils.o common/timer.o common/coords.o common/util.o

SRCS_SV = server/server.cpp server/network.cpp server/settings.cpp \
	server/sighandle.cpp server/players.cpp server/log.cpp server/map.cpp \
	server/game.cpp server/viewpoint.cpp server/actives.cpp server/ent.cpp \
	server/noccent.cpp server/occent.cpp server/spiral.cpp server/chores.cpp \
	server/cmds.cpp
OBJS_SV=$(subst .cpp,.o,$(SRCS_SV))

all: options client server

options:
	@echo "used CPPFLAGS = ${CPPFLAGS}"

client: $(OBJS_CL) $(OBJS_CO)
	$(CXX) -o yuxtapa_cl $(OBJS_CL) $(OBJS_CO) $(LDLIBS_CL)

server: $(OBJS_SV) $(OBJS_CO)
	$(CXX) -o yuxtapa_sv $(OBJS_SV) $(OBJS_CO) $(LDLIBS_SV)

mrbrown: $(OBJS_MB)
	$(CXX) -o mrbrown $(OBJS_MB) $(LDLIBS_MB)

clean:
	$(RM) $(OBJS_CL) $(OBJS_SV) $(OBJS_CO) $(OBJS_MB)

dist: clean
	@echo creating dist tarball
	@mkdir -p yuxtapa-${VERSION}
	@cp -R LICENSE Makefile README vhist.html common client server manual \
	tmplates yuxtapa-${VERSION}
	@tar -czf yuxtapa-${VERSION}.tar.gz yuxtapa-${VERSION}
	@rm -rf yuxtapa-${VERSION}

common/timer.o: common/timer.cpp
common/netutils.o: common/netutils.cpp
common/utfstr.o: common/utfstr.cpp
common/confdir.o: common/confdir.cpp
common/coords.o: common/coords.cpp
common/util.o: common/util.cpp

client/base.o: client/base.cpp
client/class_cpv.o: client/class_cpv.cpp
client/client.o: client/client.cpp
client/input.o: client/input.cpp
client/msg.o: client/msg.cpp
client/network.o: client/network.cpp
client/settings.o: client/settings.cpp
client/view.o: client/view.cpp

server/server.o: server/server.cpp
server/network.o: server/network.cpp
server/settings.o: server/settings.cpp
server/sighandle.o: server/sighandle.cpp
server/players.o: server/players.cpp
server/log.o: server/log.cpp
server/map.o: server/map.cpp
server/game.o: server/game.cpp
server/viewpoint.o: server/viewpoint.cpp
server/actives.o: server/actives.cpp
server/ent.o: server/ent.cpp
server/noccent.o: server/noccent.cpp
server/occent.o: server/occent.cpp
server/spiral.o: server/spiral.cpp
server/chores.o: server/chores.cpp
server/cmds.o: server/cmds.cpp

testes/mrbrown.o: testes/mrbrown.cpp


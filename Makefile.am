AUTOMAKE_OPTIONS = foreign subdir-objects

if WANT_CLIENT
CLIENT_APP = yuxtapa_cl
else
CLIENT_APP =
endif

if WANT_SERVER
SERVER_APP = yuxtapa_sv
else
SERVER_APP =
endif

bin_PROGRAMS = $(SERVER_APP) $(CLIENT_APP)

yuxtapa_cl_SOURCES = client/base.cpp client/base.h client/class_cpv.cpp client/class_cpv.h \
	client/client.cpp client/input.cpp client/input.h client/msg.cpp client/msg.h \
	client/network.cpp client/network.h client/settings.cpp client/settings.h \
	client/view.cpp client/view.h client/colourdef.h \
	common/timer.h common/timer.cpp \
	common/netutils.h common/netutils.cpp common/utfstr.h common/utfstr.cpp \
	common/confdir.cpp common/confdir.h common/coords.h common/coords.cpp \
	common/col_codes.h common/los_lookup.h common/util.cpp common/util.h
yuxtapa_cl_LDFLAGS = -lncursesw -lz

yuxtapa_sv_SOURCES = server/server.cpp server/network.cpp server/network.h \
	server/settings.cpp server/settings.h server/sighandle.cpp server/sighandle.h \
	server/players.cpp server/players.h server/log.h server/log.cpp server/map.h \
	server/map.cpp server/game.cpp server/game.h server/viewpoint.cpp \
	server/viewpoint.h server/actives.h server/actives.cpp server/ent.h \
	server/ent.cpp server/noccent.h server/noccent.cpp server/occent.cpp \
	server/occent.h server/spiral.h server/spiral.cpp server/chores.cpp \
	server/chores.h server/cmds.h server/cmds.cpp \
	common/timer.h common/timer.cpp common/los_lookup.h common/col_codes.h \
	common/netutils.h common/netutils.cpp common/coords.h common/coords.cpp \
	common/utfstr.h common/utfstr.cpp common/confdir.cpp common/confdir.h \
	common/util.cpp common/util.h
yuxtapa_sv_LDFLAGS = -lz -lrt


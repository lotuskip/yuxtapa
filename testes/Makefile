CXX=g++
CPPFLAGS=-O2 -DBE_QUIET
LDLIBS=-lz

SRCS=mrbrown.cpp ../common/netutils.cpp ../common/timer.cpp
OBJS=$(subst .cpp,.o,$(SRCS))

all: ../mrbrown

../mrbrown: $(OBJS)
	g++ -o ../mrbrown $(OBJS) $(LDLIBS)

../common/timer.o: ../common/timer.cpp
../common/netutils.o: ../common/netutils.cpp
mrbrown.o: mrbrown.cpp

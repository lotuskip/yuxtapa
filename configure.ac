AC_PREREQ([2.65])
AC_INIT([yuxtapa], [1])
AM_INIT_AUTOMAKE
AC_OUTPUT(Makefile)
AC_LANG([C++])

AC_PROG_CXXCPP
AC_PROG_CXX

AC_ARG_ENABLE([server],
     [  --disable-server    Do not build the server],
     [case "${enableval}" in
       yes) server=true ;;
       no)  server=false ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --disable-server]) ;;
     esac],[server=true])
AM_CONDITIONAL([WANT_SERVER], [test x$server = xtrue])

AC_ARG_ENABLE([client],
     [  --disable-client    Do not build the client],
     [case "${enableval}" in
       yes) client=true ;;
       no)  client=false ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --disable-client]) ;;
     esac],[client=true])
AM_CONDITIONAL([WANT_CLIENT], [test x$client = xtrue])

# overwrite CXXFLAGS. If you think this violates some goddamn principle of autotools, go tell that to someone who cares
AC_ARG_ENABLE([debug],
     [  --enable-debug     Turn on debugging],
	 debug="yes",
	 debug="no")
if test "x$debug" = "xyes"; then
    CXXFLAGS="-g -O1 -DDEBUG -Wall -pedantic -Wno-char-subscripts -fsigned-char"
else
	CXXFLAGS="-O2 -g0 -fsigned-char"
fi
AC_OUTPUT


INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) -DDBUS_COMPILATION

dbusincludedir=$(includedir)/dbus-1.0/dbus

lib_LTLIBRARIES=libdbus-1.la

dbusinclude_HEADERS=				\
	dbus.h					\
	dbus-macros.h				\
	dbus-memory.h				\
	dbus-message.h				\
	dbus-types.h

libdbus_1_la_SOURCES=				\
	dbus-memory.c				\
	dbus-message.c

## this library is linked into both libdbus and the bus 
## itself, but does not export any symbols from libdbus.
## It contains utility functions not in the public API.

noinst_LTLIBRARIES=libdbus-convenience.la

libdbus_convenience_la_SOURCES=			\
	dbus-hash.c				\
	dbus-hash.h				\
	dbus-internals.c			\
	dbus-internals.h

libdbus_1_la_LIBADD=  $(DBUS_CLIENT_LIBS) libdbus-convenience.la
## don't export symbols that start with "_" (we use this 
## convention for internal symbols)
libdbus_1_la_LDFLAGS= -export-symbols-regex "^[[^_]].*"

if DBUS_BUILD_TESTS
noinst_PROGRAMS=dbus-test

dbus_test_SOURCES=				\
	dbus-test.c				\
	dbus-test.h

dbus_test_LDADD= $(DBUS_CLIENT_LIBS) libdbus-convenience.la libdbus-1.la

## so that "make check" works
TESTS=dbus-test
endif

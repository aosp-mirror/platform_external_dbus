
INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) -DDBUS_COMPILATION

dbusincludedir=$(includedir)/dbus-1.0/dbus

lib_LTLIBRARIES=libdbus-1.la

dbusinclude_HEADERS=				\
	dbus.h					\
	dbus-errors.h				\
	dbus-macros.h				\
	dbus-memory.h				\
	dbus-message.h				\
	dbus-server.h				\
	dbus-threads.h				\
	dbus-types.h

libdbus_1_la_SOURCES=				\
	dbus-connection.c			\
	dbus-connection-internal.h		\
	dbus-errors.c				\
	dbus-memory.c				\
	dbus-message.c				\
	dbus-message-internal.h			\
	dbus-server.c				\
	dbus-server-protected.h			\
	dbus-server-unix.c			\
	dbus-server-unix.h			\
	dbus-threads.c				\
	dbus-transport.c			\
	dbus-transport.h			\
	dbus-transport-protected.h		\
	dbus-transport-unix.c			\
	dbus-transport-unix.h			\
	dbus-watch.c				\
	dbus-watch.h


## this library is linked into both libdbus and the bus 
## itself, but does not export any symbols from libdbus.
## i.e. the point is to contain symbols that we don't 
## want the shared lib to export, but we do want the 
## message bus to be able to use.

noinst_LTLIBRARIES=libdbus-convenience.la

libdbus_convenience_la_SOURCES=			\
	dbus-hash.c				\
	dbus-hash.h				\
	dbus-internals.c			\
	dbus-internals.h			\
	dbus-list.c				\
	dbus-list.h				\
	dbus-string.c				\
	dbus-string.h				\
	dbus-sysdeps.c				\
	dbus-sysdeps.h

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

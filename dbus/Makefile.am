
INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) -DDBUS_COMPILATION		\
	-DDBUS_SYSTEM_BUS_PATH=\""$(localstatedir)/@DBUS_SYSTEM_SOCKET@"\"

dbusincludedir=$(includedir)/dbus-1.0/dbus

lib_LTLIBRARIES=libdbus-1.la

dbusinclude_HEADERS=				\
	dbus.h					\
	dbus-address.h				\
	dbus-bus.h				\
	dbus-connection.h			\
	dbus-dict.h				\
	dbus-errors.h				\
	dbus-macros.h				\
	dbus-memory.h				\
	dbus-message.h				\
	dbus-message-handler.h			\
	dbus-protocol.h				\
	dbus-server.h				\
	dbus-threads.h				\
	dbus-types.h

DBUS_SOURCES=					\
	dbus-address.c				\
	dbus-auth.c				\
	dbus-auth.h				\
	dbus-auth-script.c			\
	dbus-auth-script.h			\
	dbus-bus.c				\
	dbus-connection.c			\
	dbus-connection-internal.h		\
	dbus-dict.c				\
	dbus-errors.c				\
	dbus-keyring.c				\
	dbus-keyring.h				\
	dbus-message.c				\
	dbus-message-handler.c			\
	dbus-message-internal.h			\
	dbus-resources.c			\
	dbus-resources.h			\
	dbus-server.c				\
	dbus-server-protected.h			\
	dbus-server-debug.c			\
	dbus-server-debug.h			\
	dbus-server-debug-pipe.c		\
	dbus-server-debug-pipe.h		\
	dbus-server-unix.c			\
	dbus-server-unix.h			\
	dbus-sha.c				\
	dbus-sha.h				\
	dbus-test.c				\
	dbus-test.h				\
	dbus-timeout.c				\
	dbus-timeout.h				\
	dbus-threads.c				\
	dbus-transport.c			\
	dbus-transport.h			\
	dbus-transport-debug.c			\
	dbus-transport-debug.h			\
	dbus-transport-protected.h		\
	dbus-transport-unix.c			\
	dbus-transport-unix.h			\
	dbus-watch.c				\
	dbus-watch.h

##	dbus-md5.c				\
##	dbus-md5.h				\

UTIL_SOURCES=					\
	dbus-dataslot.c				\
	dbus-dataslot.h				\
	dbus-hash.c				\
	dbus-hash.h				\
	dbus-internals.c			\
	dbus-internals.h			\
	dbus-list.c				\
	dbus-list.h				\
	dbus-marshal.c				\
	dbus-marshal.h				\
	dbus-memory.c				\
	dbus-mempool.c				\
	dbus-mempool.h				\
	dbus-message-builder.c			\
	dbus-message-builder.h			\
	dbus-string.c				\
	dbus-string.h				\
	dbus-string-private.h			\
	dbus-sysdeps.c				\
	dbus-sysdeps.h

libdbus_1_la_SOURCES=				\
	$(DBUS_SOURCES)				\
	$(UTIL_SOURCES)

libdbus_convenience_la_SOURCES=			\
	$(DBUS_SOURCES)				\
	$(UTIL_SOURCES)

## this library is the same as libdbus, but exports all the symbols
## and is only used for static linking within the dbus package.
noinst_LTLIBRARIES=libdbus-convenience.la

libdbus_1_la_LIBADD= $(DBUS_CLIENT_LIBS)
## don't export symbols that start with "_" (we use this 
## convention for internal symbols)
libdbus_1_la_LDFLAGS= -export-symbols-regex "^[^_].*"

## note that TESTS has special meaning (stuff to use in make check)
## so if adding tests not to be run in make check, don't add them to 
## TESTS
if DBUS_BUILD_TESTS
TESTS_ENVIRONMENT=DBUS_TEST_DATA=$(top_srcdir)/test/data
TESTS=dbus-test 
else
TESTS=
endif

## we use noinst_PROGRAMS not check_PROGRAMS so that we build 
## even when not doing "make check"
noinst_PROGRAMS=$(TESTS)

dbus_test_SOURCES=				\
	dbus-test-main.c

dbus_test_LDADD= $(DBUS_CLIENT_LIBS) libdbus-1.la

## mop up the gcov files
clean-local:
	/bin/rm *.bb *.bbg *.da *.gcov || true

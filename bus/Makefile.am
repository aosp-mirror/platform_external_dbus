
INCLUDES=-I$(top_srcdir) $(DBUS_BUS_CFLAGS) \
	-DDAEMON_NAME=\"dbus-daemon-1\"  -DDBUS_COMPILATION

EFENCE=

bin_PROGRAMS=dbus-daemon-1

noinst_LTLIBRARIES=libdbus-daemon.la

libdbus_daemon_la_SOURCES=			\
	activation.c				\
	activation.h				\
	bus.c					\
	bus.h					\
	connection.c				\
	connection.h				\
	desktop-file.c				\
	desktop-file.h				\
	dispatch.c				\
	dispatch.h				\
	driver.c				\
	driver.h				\
	loop.c					\
	loop.h					\
	services.c				\
	services.h				\
	test.c					\
	test.h					\
	utils.c					\
	utils.h


libdbus_daemon_la_LIBADD=				\
	$(top_builddir)/dbus/libdbus-convenience.la

dbus_daemon_1_SOURCES=				\
	main.c					

dbus_daemon_1_LDADD=					\
	$(EFENCE)					\
	$(DBUS_BUS_LIBS)				\
	$(top_builddir)/bus/libdbus-daemon.la		\
	$(top_builddir)/dbus/libdbus-1.la

## note that TESTS has special meaning (stuff to use in make check)
## so if adding tests not to be run in make check, don't add them to 
## TESTS
if DBUS_BUILD_TESTS
TESTS_ENVIRONMENT=DBUS_TEST_DATA=$(top_srcdir)/test/data
TESTS=bus-test 
else
TESTS=
endif

## we use noinst_PROGRAMS not check_PROGRAMS so that we build 
## even when not doing "make check"
noinst_PROGRAMS=$(TESTS)

bus_test_SOURCES=				\
	test-main.c

bus_test_LDADD= $(top_builddir)/dbus/libdbus-convenience.la libdbus-daemon.la

## mop up the gcov files
clean-local:
	/bin/rm *.bb *.bbg *.da *.gcov || true

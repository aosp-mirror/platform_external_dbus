INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_GLIB_CFLAGS) $(DBUS_GLIB_TOOL_CFLAGS) -DDBUS_COMPILATION=1

dbusincludedir=$(includedir)/dbus-1.0/dbus

lib_LTLIBRARIES=libdbus-glib-1.la

dbusinclude_HEADERS=				\
	dbus-glib.h				\
	dbus-gproxy.h

libdbus_glib_1_la_SOURCES = 			\
	dbus-gmain.c				\
	dbus-gobject.c				\
	dbus-gproxy.c				\
	dbus-gtest.c				\
	dbus-gtest.h				\
	dbus-gthread.c

libdbus_glib_1_la_LIBADD= $(DBUS_GLIB_LIBS) $(top_builddir)/dbus/libdbus-1.la

bin_PROGRAMS=dbus-glib-tool

dbus_glib_tool_SOURCES =			\
	dbus-gidl.c				\
	dbus-gidl.h				\
	dbus-glib-tool.c			\
	dbus-gloader-expat.c			\
	dbus-gparser.c				\
	dbus-gparser.h				\
	dbus-gtool-test.h


dbus_glib_tool_LDADD= $(DBUS_GLIB_TOOL_LIBS) libdbus-glib-1.la $(top_builddir)/dbus/libdbus-1.la

if DBUS_BUILD_TESTS

if HAVE_GLIB_THREADS
THREAD_APPS=test-thread-server test-thread-client test-profile

test_thread_server_SOURCES=				\
	test-thread-server.c				\
	test-thread.h

test_thread_server_LDADD= $(DBUS_GLIB_THREADS_LIBS) $(top_builddir)/glib/libdbus-glib-1.la 

test_thread_client_SOURCES=				\
	test-thread-client.c				\
	test-thread.h

test_thread_client_LDADD= $(DBUS_GLIB_THREADS_LIBS) $(top_builddir)/glib/libdbus-glib-1.la 
endif

## we use noinst_PROGRAMS not check_PROGRAMS for TESTS so that we
## build even when not doing "make check"
noinst_PROGRAMS= $(TESTS) test-dbus-glib $(THREAD_APPS)

test_dbus_glib_SOURCES=				\
	test-dbus-glib.c

test_dbus_glib_LDADD= $(top_builddir)/glib/libdbus-glib-1.la

## note that TESTS has special meaning (stuff to use in make check)
## so if adding tests not to be run in make check, don't add them to 
## TESTS
TESTS_ENVIRONMENT=DBUS_TEST_DATA=$(top_builddir)/test/data DBUS_TEST_HOMEDIR=$(top_builddir)/dbus
TESTS=dbus-glib-test

dbus_glib_test_SOURCES=				\
	dbus-gtest-main.c

dbus_glib_test_LDADD= $(top_builddir)/glib/libdbus-glib-1.la

else
### not building tests
TESTS=

if HAVE_GLIB_THREADS
noinst_PROGRAMS=test-profile
endif

endif

if HAVE_GLIB_THREADS
test_profile_SOURCES=				\
	test-profile.c

test_profile_LDADD= $(DBUS_GLIB_THREADS_LIBS) $(top_builddir)/glib/libdbus-glib-1.la 
endif
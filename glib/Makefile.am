INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_GLIB_CFLAGS) $(DBUS_GLIB_TOOL_CFLAGS) -DDBUS_COMPILATION=1 -DDBUS_LOCALEDIR=\"$(prefix)/@DATADIRNAME@/locale\"

lib_LTLIBRARIES=libdbus-glib-1.la

libdbus_glib_1_la_SOURCES = 			\
	dbus-glib.c				\
	dbus-gmain.c				\
	dbus-gobject.c				\
	dbus-gproxy.c				\
	dbus-gtest.c				\
	dbus-gtest.h				\
	dbus-gthread.c				\
	dbus-gutils.c				\
	dbus-gutils.h				\
	dbus-gvalue.c				\
	dbus-gvalue.h

libdbus_glib_1_la_LIBADD= $(DBUS_GLIB_LIBS) $(top_builddir)/dbus/libdbus-1.la
## don't export symbols that start with "_" (we use this 
## convention for internal symbols)
libdbus_glib_1_la_LDFLAGS= -export-symbols-regex "^[^_].*"

# convenience lib used here and by dbus-viewer
noinst_LTLIBRARIES=libdbus-gtool.la

libdbus_gtool_la_SOURCES =			\
	dbus-gidl.c				\
	dbus-gidl.h				\
	dbus-gloader-expat.c			\
	dbus-gparser.c				\
	dbus-gparser.h				\
	dbus-gutils.c				\
	dbus-gutils.h

libdbus_gtool_la_LIBADD = libdbus-glib-1.la

bin_PROGRAMS=dbus-glib-tool

dbus_glib_tool_SOURCES =			\
	dbus-glib-tool.c

dbus_glib_tool_LDADD= -lexpat libdbus-gtool.la

if DBUS_BUILD_TESTS

## we use noinst_PROGRAMS not check_PROGRAMS for TESTS so that we
## build even when not doing "make check"
noinst_PROGRAMS= $(TESTS)

## note that TESTS has special meaning (stuff to use in make check)
## so if adding tests not to be run in make check, don't add them to 
## TESTS
TESTS_ENVIRONMENT=DBUS_TEST_DATA=$(top_builddir)/test/data DBUS_TEST_HOMEDIR=$(top_builddir)/dbus
TESTS=dbus-glib-test

## FIXME we aren't running dbus-glib-tool --self-test

dbus_glib_test_SOURCES=			\
	dbus-gtest-main.c

dbus_glib_test_LDADD= $(top_builddir)/glib/libdbus-glib-1.la

else
### not building tests
TESTS=

endif


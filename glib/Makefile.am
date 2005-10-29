SUBDIRS = . examples

INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_GLIB_CFLAGS) $(DBUS_GLIB_TOOL_CFLAGS) -DDBUS_COMPILATION=1 -DDBUS_LOCALEDIR=\"$(prefix)/@DATADIRNAME@/locale\"

lib_LTLIBRARIES=libdbus-glib-1.la

dbus-glib-error-switch.h: $(top_srcdir)/dbus/dbus-protocol.h make-dbus-glib-error-switch.sh
	$(LIBTOOL) --mode=execute $(srcdir)/make-dbus-glib-error-switch.sh $(top_srcdir)/dbus/dbus-protocol.h $@

BUILT_SOURCES = dbus-glib-error-switch.h

CLEANFILES = $(BUILT_SOURCES)

DBUS_GLIB_INTERNALS = \
	dbus-gtype-specialized.c	        \
	dbus-gutils.c				\
	dbus-gutils.h				\
	dbus-gsignature.c			\
	dbus-gsignature.h			\
	dbus-gvalue.h				\
	dbus-gvalue-utils.c			\
	dbus-gvalue-utils.h

libdbus_glib_1_la_SOURCES = 			\
	dbus-glib-error-switch.h		\
	dbus-glib.c				\
	dbus-gmain.c				\
	dbus-gmarshal.c				\
	dbus-gmarshal.h				\
	dbus-gobject.c				\
	dbus-gobject.h				\
	dbus-gproxy.c				\
	dbus-gtest.c				\
	dbus-gtest.h				\
	dbus-gvalue.c				\
	dbus-gvalue.h				\
	dbus-gthread.c				\
	$(DBUS_GLIB_INTERNALS)

libdbus_glib_HEADERS =                  \
	dbus-gtype-specialized.h

libdbus_glibdir = $(includedir)/dbus-1.0/dbus

libdbus_glib_1_la_LIBADD= $(top_builddir)/dbus/libdbus-1.la $(DBUS_GLIB_LIBS)
## don't export symbols that start with "_" (we use this 
## convention for internal symbols)
libdbus_glib_1_la_LDFLAGS= -export-symbols-regex "^[^_].*" -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) -no-undefined

# convenience lib used here and by dbus-viewer
noinst_LTLIBRARIES=libdbus-gtool.la

libdbus_gtool_la_SOURCES = $(DBUS_GLIB_INTERNALS) \
	dbus-gidl.c				\
	dbus-gidl.h				\
	dbus-gloader-expat.c			\
	dbus-gparser.c				\
	dbus-gparser.h

libdbus_gtool_la_LIBADD = libdbus-glib-1.la

bin_PROGRAMS=dbus-binding-tool

dbus_binding_tool_SOURCES =			\
	dbus-binding-tool-glib.h		\
	dbus-binding-tool-glib.c		\
	dbus-glib-tool.h			\
	dbus-glib-tool.c

dbus_binding_tool_LDADD= libdbus-gtool.la $(DBUS_GLIB_LIBS) -lexpat

## we just rebuilt these manually and check them into cvs; easier than
## convincing automake/make to do this properly
regenerate-built-sources:
	@GLIB_GENMARSHAL@ --prefix=_dbus_g_marshal dbus-gmarshal.list --header > dbus-gmarshal.h && \
	echo '#include "dbus-gmarshal.h"' > dbus-gmarshal.c &&					    \
        @GLIB_GENMARSHAL@ --prefix=_dbus_g_marshal dbus-gmarshal.list --body >> dbus-gmarshal.c

EXTRA_DIST=dbus-gmarshal.list make-dbus-glib-error-switch.sh

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


INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_GLIB_CFLAGS) -DDBUS_COMPILATION

## note that TESTS has special meaning (stuff to use in make check)
## so if adding tests not to be run in make check, don't add them to 
## TESTS
if DBUS_BUILD_TESTS
TESTS_ENVIRONMENT=DBUS_TOP_BUILDDIR=$(ABSOLUTE_TOP_BUILDDIR)
TESTS=run-test.sh
else
TESTS=
endif

EXTRA_DIST=run-test.sh test-service-glib.xml my-object-marshal.list test-service-glib.xml

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
noinst_PROGRAMS= test-dbus-glib test-service-glib $(THREAD_APPS)

test_dbus_glib_SOURCES=				\
	my-object-marshal.c                             \
	test-dbus-glib.c

test_dbus_glib_LDADD= $(DBUS_GLIB_TOOL_LIBS) $(top_builddir)/glib/libdbus-glib-1.la $(top_builddir)/glib/libdbus-gtool.la

BUILT_SOURCES = test-service-glib-glue.h test-service-glib-bindings.h my-object-marshal.c my-object-marshal.h 

test_service_glib_SOURCES=				\
	my-object-marshal.c                             \
	test-service-glib.c 

test-service-glib-glue.h: test-service-glib.xml $(top_builddir)/glib/dbus-binding-tool
	$(top_builddir)/glib/dbus-binding-tool --prefix=my_object --mode=glib-server --output=test-service-glib-glue.h $(srcdir)/test-service-glib.xml

test-service-glib-bindings.h: test-service-glib.xml $(top_builddir)/glib/dbus-binding-tool
	$(top_builddir)/glib/dbus-binding-tool --prefix=my_object --mode=glib-client --output=test-service-glib-bindings.h $(srcdir)/test-service-glib.xml

my-object-marshal.c: Makefile my-object-marshal.list
	@GLIB_GENMARSHAL@ --prefix=my_object_marshal $(srcdir)/my-object-marshal.list --header --body > my-object-marshal.c

my-object-marshal.h: Makefile my-object-marshal.list
	@GLIB_GENMARSHAL@ --prefix=my_object_marshal $(srcdir)/my-object-marshal.list --header > my-object-marshal.h


CLEANFILES = $(BUILT_SOURCES)

test_service_glib_LDADD= $(top_builddir)/glib/libdbus-glib-1.la $(DBUS_GLIB_THREADS_LIBS) 

else
### not building tests

if HAVE_GLIB_THREADS
noinst_PROGRAMS=test-profile
endif

endif

if HAVE_GLIB_THREADS
test_profile_SOURCES=				\
	test-profile.c

test_profile_LDADD= $(DBUS_GLIB_THREADS_LIBS) $(top_builddir)/glib/libdbus-glib-1.la 
endif

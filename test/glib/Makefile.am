INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_GLIB_CFLAGS)

## note that TESTS has special meaning (stuff to use in make check)
## so if adding tests not to be run in make check, don't add them to 
## TESTS
if DBUS_BUILD_TESTS
TESTS_ENVIRONMENT=DBUS_TOP_BUILDDIR=$(ABSOLUTE_TOP_BUILDDIR)
TESTS=run-test.sh
else
TESTS=
endif

EXTRA_DIST=run-test.sh

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
	test-dbus-glib.c

test_dbus_glib_LDADD= $(top_builddir)/glib/libdbus-glib-1.la

test_service_glib_SOURCES=				\
	test-service-glib.c

test_service_glib_LDADD= $(top_builddir)/glib/libdbus-glib-1.la

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

INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_GLIB_CFLAGS) -I$(top_srcdir)/glib

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
noinst_PROGRAMS= test-dbus-glib $(THREAD_APPS)

test_dbus_glib_SOURCES=				\
	test-dbus-glib.c

test_dbus_glib_LDADD= $(top_builddir)/glib/libdbus-glib-1.la

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
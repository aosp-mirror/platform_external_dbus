INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_GLIB_CFLAGS)

dbusincludedir=$(includedir)/dbus-1.0/dbus

lib_LTLIBRARIES=libdbus-glib-1.la

dbusinclude_HEADERS=				\
	dbus-glib.h

libdbus_glib_1_la_SOURCES = 			\
	dbus-gmain.c				\
	dbus-gthread.c

libdbus_glib_1_la_LIBADD= $(DBUS_GLIB_LIBS) $(top_builddir)/dbus/libdbus-1.la


if DBUS_BUILD_TESTS

if HAVE_GLIB_THREADS
    THREAD_APPS=test-thread-server test-thread-client
endif

noinst_PROGRAMS= test-dbus-glib $(THREAD_APPS)

test_dbus_glib_SOURCES=				\
	test-dbus-glib.c

test_dbus_glib_LDADD= $(top_builddir)/glib/libdbus-glib-1.la

if HAVE_GLIB_THREADS
test_thread_server_SOURCES=				\
	test-thread-server.c				\
	test-thread.h

test_thread_server_LDADD= $(DBUS_GLIB_THREADS_LIBS) $(top_builddir)/glib/libdbus-glib-1.la 

test_thread_client_SOURCES=				\
	test-thread-client.c				\
	test-thread.h

test_thread_client_LDADD= $(DBUS_GLIB_THREADS_LIBS) $(top_builddir)/glib/libdbus-glib-1.la 
endif

endif

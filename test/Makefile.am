
INCLUDES=-I$(top_srcdir) $(DBUS_TEST_CFLAGS) 

if DBUS_BUILD_TESTS
TEST_BINARIES=echo-client echo-server
else
TEST_BINARIES=
endif

noinst_PROGRAMS= $(TEST_BINARIES)

echo_client_SOURCES=				\
	echo-client.c				\
	watch.c					\
	watch.h

echo_server_SOURCES=				\
	echo-server.c				\
	watch.c					\
	watch.h

TEST_LIBS=$(DBUS_TEST_LIBS) $(top_builddir)/dbus/libdbus-convenience.la $(top_builddir)/dbus/libdbus-1.la

echo_client_LDADD=$(TEST_LIBS)
echo_server_LDADD=$(TEST_LIBS)


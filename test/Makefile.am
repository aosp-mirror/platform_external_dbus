
INCLUDES=-I$(top_srcdir) $(DBUS_TEST_CFLAGS) 

if DBUS_BUILD_TESTS
TEST_BINARIES=echo-client echo-server unbase64 break-loader spawn-test
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

unbase64_SOURCES=				\
	unbase64.c

# bus_test_SOURCES =				\
# 	debug-thread.c				\
# 	debug-thread.h				\
# 	bus-test.c				\
# 	bus-test-loop.c				\
# 	bus-test-loop.h

break_loader_SOURCES=				\
	break-loader.c

spawn_test_SOURCES=				\
	spawn-test.c

TEST_LIBS=$(DBUS_TEST_LIBS) $(top_builddir)/dbus/libdbus-convenience.la $(top_builddir)/dbus/libdbus-1.la

echo_client_LDADD=$(TEST_LIBS)
echo_server_LDADD=$(TEST_LIBS)
unbase64_LDADD=$(TEST_LIBS)
break_loader_LDADD= $(TEST_LIBS)
#bus_test_LDADD=$(TEST_LIBS) $(top_builddir)/bus/libdbus-daemon.la
spawn_test_LDADD=$(TEST_LIBS)

dist-hook:
	DIRS="data data/valid-messages data/invalid-messages data/incomplete-messages data/auth data/sha-1"; \
	for D in $$DIRS; do									   \
		test -d $(distdir)/$$D || mkdir $(distdir)/$$D ;				   \
	done ;											   \
	FILES=`find -name "*.message" -o -name "*.message-raw" -o -name "*.auth-script" -o -name "*.sha1" -o -name "*.txt"` ;	   \
        for F in $$FILES; do									   \
                echo '-- Disting file '$$F ;							   \
		cp $$F $(distdir)/$$F ;								   \
	done

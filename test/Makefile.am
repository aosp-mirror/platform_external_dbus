
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

dist-hook:
	DIRS="data data/valid-messages data/invalid-messages data/incomplete-messages" ; \
	for D in $$DIRS; do						\
		test -d $(distdir)/$$D || mkdir $(distdir)/$$D ;	\
	done ;								\
	FILES=`find -name "*.message"` ;				\
        for F in $$FILES; do						\
                echo '-- Disting file '$$F ;				\
		cp $$F $(distdir)/$$F ;					\
	done


configdir=$(sysconfdir)/dbus-1

INCLUDES=-I$(top_srcdir) $(DBUS_BUS_CFLAGS)				\
	-DDAEMON_NAME=\"dbus-daemon\"  -DDBUS_COMPILATION		\
	-DDBUS_SYSTEM_CONFIG_FILE=\""$(configdir)/system.conf"\"	\
	-DDBUS_SESSION_CONFIG_FILE=\""$(configdir)/session.conf"\"

EFENCE=

CONFIG_IN_FILES=				\
	session.conf.in				\
	system.conf.in

config_DATA=					\
	session.conf				\
	system.conf

if DBUS_USE_LIBXML
XML_SOURCES=config-loader-libxml.c
endif
if DBUS_USE_EXPAT
XML_SOURCES=config-loader-expat.c
endif

if DBUS_BUS_ENABLE_KQUEUE
DIR_WATCH_SOURCE=dir-watch-kqueue.c
else
if DBUS_BUS_ENABLE_DNOTIFY_ON_LINUX
DIR_WATCH_SOURCE=dir-watch-dnotify.c
else
DIR_WATCH_SOURCE=dir-watch-default.c
endif
endif

BUS_SOURCES=					\
	activation.c				\
	activation.h				\
	bus.c					\
	bus.h					\
	config-parser.c				\
	config-parser.h				\
	connection.c				\
	connection.h				\
	desktop-file.c				\
	desktop-file.h				\
	$(DIR_WATCH_SOURCE)			\
	dir-watch.h				\
	dispatch.c				\
	dispatch.h				\
	driver.c				\
	driver.h				\
	expirelist.c				\
	expirelist.h				\
	policy.c				\
	policy.h				\
	selinux.h				\
	selinux.c				\
	services.c				\
	services.h				\
	signals.c				\
	signals.h				\
	test.c					\
	test.h					\
	utils.c					\
	utils.h					\
	$(XML_SOURCES)

dbus_daemon_SOURCES=				\
	$(BUS_SOURCES)				\
	main.c					

dbus_daemon_LDADD=					\
	$(EFENCE)					\
	$(DBUS_BUS_LIBS)				\
	$(top_builddir)/dbus/libdbus-convenience.la

dbus_daemon_LDFLAGS=@R_DYNAMIC_LDFLAG@

## note that TESTS has special meaning (stuff to use in make check)
## so if adding tests not to be run in make check, don't add them to 
## TESTS
if DBUS_BUILD_TESTS
TESTS_ENVIRONMENT=DBUS_TEST_DATA=$(top_builddir)/test/data DBUS_TEST_HOMEDIR=$(top_builddir)/dbus DBUS_FATAL_WARNINGS=1 DBUS_BLOCK_ON_ABORT=1
TESTS=bus-test
else
TESTS=
endif

## we use noinst_PROGRAMS not check_PROGRAMS so that we build 
## even when not doing "make check"
noinst_PROGRAMS=$(TESTS) dbus-daemon

bus_test_SOURCES=				\
	$(BUS_SOURCES)				\
	test-main.c

bus_test_LDADD=$(top_builddir)/dbus/libdbus-convenience.la $(DBUS_BUS_LIBS)
bus_test_LDFLAGS=@R_DYNAMIC_LDFLAG@

## mop up the gcov files
clean-local:
	/bin/rm *.bb *.bbg *.da *.gcov || true

uninstall-hook:
	rm -f $(DESTDIR)$(DBUS_DAEMONDIR)/dbus-daemon

install-data-hook:
	if test '!' -d $(DESTDIR)$(DBUS_DAEMONDIR); then \
		$(mkinstalldirs) $(DESTDIR)$(DBUS_DAEMONDIR); \
		chmod 755 $(DESTDIR)$(DBUS_DAEMONDIR); \
	fi
	$(INSTALL_PROGRAM) dbus-daemon $(DESTDIR)$(DBUS_DAEMONDIR)
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/run/dbus
	$(mkinstalldirs) $(DESTDIR)$(configdir)/system.d
	$(mkinstalldirs) $(DESTDIR)$(datadir)/dbus-1/services

#### Init scripts fun
SCRIPT_IN_FILES=messagebus.in \
		rc.messagebus.in

## Red Hat start
if DBUS_INIT_SCRIPTS_RED_HAT

initddir=$(sysconfdir)/rc.d/init.d

initd_SCRIPTS= 	\
	messagebus

endif
 ## Red Hat end

## Slackware start
if DBUS_INIT_SCRIPTS_SLACKWARE

initddir=$(sysconfdir)/rc.d/

initd_SCRIPTS= 	\
	rc.messagebus

endif
## Slackware end

MAN_IN_FILES=dbus-daemon.1.in
man_MANS = dbus-daemon.1

#### Extra dist 

EXTRA_DIST=$(CONFIG_IN_FILES) $(SCRIPT_IN_FILES) $(man_MANS) $(MAN_IN_FILES)

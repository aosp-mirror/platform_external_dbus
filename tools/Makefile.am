INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_X_CFLAGS) -DDBUS_LOCALEDIR=\"@EXPANDED_DATADIR@/locale\" -DDBUS_COMPILATION -DDBUS_DAEMONDIR=\"@DBUS_DAEMONDIR@\"

bin_PROGRAMS=dbus-send dbus-monitor dbus-launch dbus-cleanup-sockets dbus-uuidgen

dbus_send_SOURCES=				\
	dbus-print-message.c			\
	dbus-print-message.h			\
	dbus-send.c

dbus_monitor_SOURCES=				\
	dbus-monitor.c				\
	dbus-print-message.c			\
	dbus-print-message.h

dbus_launch_SOURCES=				\
	dbus-launch.c				\
	dbus-launch-x11.c

dbus_cleanup_sockets_SOURCES=			\
	dbus-cleanup-sockets.c

dbus_uuidgen_SOURCES=				\
	dbus-uuidgen.c

dbus_send_LDADD= $(top_builddir)/dbus/libdbus-1.la
dbus_monitor_LDADD= $(top_builddir)/dbus/libdbus-1.la
dbus_uuidgen_LDADD= $(top_builddir)/dbus/libdbus-1.la
dbus_launch_LDADD= $(DBUS_X_LIBS)

man_MANS = dbus-send.1 dbus-monitor.1 dbus-launch.1 dbus-cleanup-sockets.1 dbus-uuidgen.1
EXTRA_DIST = $(man_MANS) run-with-tmp-session-bus.sh
CLEANFILES = 				\
	run-with-tmp-session-bus.conf

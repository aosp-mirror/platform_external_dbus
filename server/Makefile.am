
INCLUDES=-I$(top_srcdir) $(DBUS_SERVER_CFLAGS) \
	-DDAEMON_NAME=\"dbus-daemon-1\"

EFENCE=

bin_PROGRAMS=dbus-daemon-1

dbus_daemon_1_SOURCES=				\
	main.c

dbus_daemon_1_LDADD= 				\
	$(EFENCE)				\
	$(DBUS_SERVER_LIBS)			\
	$(top_builddir)/dbus/libdbus-convenience.la

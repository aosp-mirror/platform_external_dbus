
INCLUDES=-I$(top_srcdir) $(DBUS_BUS_CFLAGS) \
	-DDAEMON_NAME=\"dbus-daemon-1\"  -DDBUS_COMPILATION

EFENCE=

bin_PROGRAMS=dbus-daemon-1

noinst_LTLIBRARIES=libdbus-daemon.la

libdbus_daemon_la_SOURCES=			\
	connection.c				\
	connection.h				\
	dispatch.c				\
	dispatch.h				\
	driver.c				\
	driver.h				\
	loop.c					\
	loop.h					\
	services.c				\
	services.h

libdbus_daemon_la_LIBADD=				\
	$(top_builddir)/dbus/libdbus-convenience.la

dbus_daemon_1_SOURCES=				\
	main.c					

dbus_daemon_1_LDADD=					\
	$(EFENCE)					\
	$(DBUS_BUS_LIBS)				\
	$(top_builddir)/bus/libdbus-daemon.la		\
	$(top_builddir)/dbus/libdbus-1.la

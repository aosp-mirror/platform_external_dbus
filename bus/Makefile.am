
INCLUDES=-I$(top_srcdir) $(DBUS_BUS_CFLAGS) \
	-DDAEMON_NAME=\"dbus-daemon-1\"  -DDBUS_COMPILATION

EFENCE=

bin_PROGRAMS=dbus-daemon-1

dbus_daemon_1_SOURCES=				\
	connection.c				\
	connection.h				\
	dispatch.c				\
	dispatch.h				\
	driver.c				\
	driver.h				\
	loop.c					\
	loop.h					\
	main.c					\
	services.c				\
	services.h

dbus_daemon_1_LDADD=					\
	$(EFENCE)					\
	$(DBUS_BUS_LIBS)				\
	$(top_builddir)/dbus/libdbus-convenience.la	\
	$(top_builddir)/dbus/libdbus-1.la

INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_GLIB_CFLAGS)

if HAVE_GLIB
GLIB_TOOLS=dbus-monitor
else
GLIB_TOOLS=
endif

bin_PROGRAMS=dbus-send $(GLIB_TOOLS) dbus-launch

dbus_send_SOURCES=				\
	dbus-send.c

dbus_monitor_SOURCES=				\
	dbus-monitor.c

dbus_launch_SOURCES=				\
	dbus-launch.c

dbus_send_LDADD= $(top_builddir)/dbus/libdbus-1.la
dbus_monitor_LDADD= $(top_builddir)/glib/libdbus-glib-1.la
dbus_launch_LDADD= $(top_builddir)/dbus/libdbus-1.la

man_MANS = dbus-send.1 dbus-monitor.1 dbus-launch.1
EXTRA_DIST = $(man_MANS)

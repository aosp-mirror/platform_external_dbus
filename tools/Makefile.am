INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_GLIB_CFLAGS) $(DBUS_X_CFLAGS) $(DBUS_GTK_CFLAGS) -DDBUS_LOCALEDIR=\"$(prefix)/@DATADIRNAME@/locale\"

if HAVE_GLIB
GLIB_TOOLS=dbus-monitor
else
GLIB_TOOLS=
endif

if HAVE_GTK
GTK_TOOLS=dbus-viewer
else
GTK_TOOLS=
endif

bin_PROGRAMS=dbus-send $(GLIB_TOOLS) dbus-launch dbus-cleanup-sockets $(GTK_TOOLS)

dbus_send_SOURCES=				\
	dbus-print-message.c			\
	dbus-print-message.h			\
	dbus-send.c

dbus_monitor_SOURCES=				\
	dbus-monitor.c				\
	dbus-print-message.c			\
	dbus-print-message.h

dbus_launch_SOURCES=				\
	dbus-launch.c

dbus_cleanup_sockets_SOURCES=			\
	dbus-cleanup-sockets.c

dbus_viewer_SOURCES=				\
	dbus-tree-view.c			\
	dbus-tree-view.h			\
	dbus-viewer.c

dbus_send_LDADD= $(top_builddir)/dbus/libdbus-1.la
dbus_monitor_LDADD= $(top_builddir)/glib/libdbus-glib-1.la
dbus_launch_LDADD= $(DBUS_X_LIBS)
dbus_viewer_LDADD= $(DBUS_GLIB_TOOL_LIBS) $(top_builddir)/glib/libdbus-gtool.la $(DBUS_GTK_LIBS)

man_MANS = dbus-send.1 dbus-monitor.1 dbus-launch.1 dbus-cleanup-sockets.1
EXTRA_DIST = $(man_MANS)



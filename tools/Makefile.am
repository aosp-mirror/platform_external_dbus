INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_GLIB_CFLAGS) $(DBUS_X_CFLAGS) $(DBUS_GTK_THREADS_CFLAGS) -DDBUS_LOCALEDIR=\"$(prefix)/@DATADIRNAME@/locale\"

if HAVE_GLIB
GLIB_TOOLS=dbus-monitor

libdbus_glib_HEADERS = dbus-glib-bindings.h
libdbus_glibdir = $(includedir)/dbus-1.0/dbus

dbus-glib-bindings.h: dbus-bus-introspect.xml $(top_builddir)/glib/dbus-binding-tool
	$(top_builddir)/glib/dbus-binding-tool --ignore-unsupported --mode=glib-client --output=dbus-glib-bindings.h dbus-bus-introspect.xml # FIXME - remove --ignore-unsupported when we can do arrays

BUILT_SOURCES = dbus-glib-bindings.h

else
GLIB_TOOLS=
endif

if HAVE_GTK
GTK_TOOLS=dbus-viewer
else
GTK_TOOLS=
endif

noinst_PROGRAMS = print-introspect

print_introspect_SOURCES = print-introspect.c
print_introspect_LDADD = $(top_builddir)/glib/libdbus-glib-1.la

run-with-tmp-session-bus.sh: $(top_builddir)/bus/dbus-daemon dbus-launch

dbus-bus-introspect.xml: $(srcdir)/run-with-tmp-session-bus.sh print-introspect $(top_builddir)/bus/dbus-daemon
	DBUS_TOP_BUILDDIR=$(top_builddir) $(srcdir)/run-with-tmp-session-bus.sh ./print-introspect org.freedesktop.DBus /org/freedesktop/DBus > dbus-bus-introspect.xml.tmp && mv dbus-bus-introspect.xml.tmp dbus-bus-introspect.xml

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
	dbus-names-model.c			\
	dbus-names-model.h			\
	dbus-tree-view.c			\
	dbus-tree-view.h			\
	dbus-viewer.c

dbus_send_LDADD= $(top_builddir)/dbus/libdbus-1.la
dbus_monitor_LDADD= $(top_builddir)/glib/libdbus-glib-1.la
dbus_launch_LDADD= $(DBUS_X_LIBS)
dbus_viewer_LDADD= $(DBUS_GLIB_TOOL_LIBS) $(top_builddir)/glib/libdbus-gtool.la $(DBUS_GTK_THREADS_LIBS)

man_MANS = dbus-send.1 dbus-monitor.1 dbus-launch.1 dbus-cleanup-sockets.1
EXTRA_DIST = $(man_MANS) run-with-tmp-session-bus.sh



INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_GLIB_CFLAGS) $(DBUS_X_CFLAGS) $(DBUS_GTK_THREADS_CFLAGS) -DDBUS_LOCALEDIR=\"$(prefix)/@DATADIRNAME@/locale\" -DDBUS_COMPILATION

if HAVE_GLIB
GLIB_TOOLS=dbus-monitor

nodist_libdbus_glib_HEADERS = dbus-glib-bindings.h
libdbus_glibdir = $(includedir)/dbus-1.0/dbus

dbus-glib-bindings.h: dbus-bus-introspect.xml $(top_builddir)/glib/dbus-binding-tool$(EXEEXT)
	$(top_builddir)/glib/dbus-binding-tool --mode=glib-client --output=dbus-glib-bindings.h dbus-bus-introspect.xml

BUILT_SOURCES = dbus-glib-bindings.h dbus-bus-introspect.xml

else
GLIB_TOOLS=
endif

if HAVE_GTK
GTK_TOOLS=dbus-viewer
else
GTK_TOOLS=
endif

if HAVE_GLIB
dbus-bus-introspect.xml: $(top_builddir)/bus/dbus-daemon$(EXEEXT) dbus-launch$(EXEEXT) dbus-send$(EXEEXT) $(top_builddir)/bus/dbus-daemon$(EXEEXT) Makefile
	DBUS_TOP_BUILDDIR=$(top_builddir) $(srcdir)/run-with-tmp-session-bus.sh ./dbus-send --print-reply=literal --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.Introspectable.Introspect > dbus-bus-introspect.xml.tmp && mv dbus-bus-introspect.xml.tmp dbus-bus-introspect.xml
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
	dbus-names-model.c			\
	dbus-names-model.h			\
	dbus-tree-view.c			\
	dbus-tree-view.h			\
	dbus-viewer.c

dbus_send_LDADD= $(top_builddir)/dbus/libdbus-1.la
dbus_monitor_LDADD= $(top_builddir)/glib/libdbus-glib-1.la
dbus_launch_LDADD= $(DBUS_X_LIBS)
dbus_viewer_LDADD= $(top_builddir)/glib/libdbus-gtool.la $(DBUS_GTK_THREADS_LIBS) $(DBUS_GLIB_TOOL_LIBS)

man_MANS = dbus-send.1 dbus-monitor.1 dbus-launch.1 dbus-cleanup-sockets.1
EXTRA_DIST = $(man_MANS) run-with-tmp-session-bus.sh
CLEANFILES = 				\
	run-with-tmp-session-bus.conf	\
	dbus-bus-introspect.xml		\
	dbus-glib-bindings.h

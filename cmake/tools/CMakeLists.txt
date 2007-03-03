
include_directories(${CMAKE_SOURCE_DIR}/..)

#nodist_libdbus_glib_HEADERS = dbus-glib-bindings.h
#libdbus_glibdir = $(includedir)/dbus-1.0/dbus

#dbus-glib-bindings.h: dbus-bus-introspect.xml $(top_builddir)/glib/dbus-binding-tool$(EXEEXT)
#	$(top_builddir)/glib/dbus-binding-tool --mode=glib-client --output=dbus-glib-bindings.h dbus-bus-introspect.xml

#BUILT_SOURCES = dbus-glib-bindings.h dbus-bus-introspect.xml

#else
#GLIB_TOOLS=
#endif

#if HAVE_GTK
#GTK_TOOLS=dbus-viewer
#else
#GTK_TOOLS=
#endif

#if HAVE_GLIB
#dbus-bus-introspect.xml: $(top_builddir)/bus/dbus-daemon$(EXEEXT) dbus-launch$(EXEEXT) dbus-send$(EXEEXT) $(top_builddir)/bus/dbus-daemon$(EXEEXT) Makefile
#	DBUS_TOP_BUILDDIR=$(top_builddir) $(srcdir)/run-with-tmp-session-bus.sh ./dbus-send --print-reply=literal --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.Introspectable.Introspect > dbus-bus-introspect.xml.tmp && mv dbus-bus-introspect.xml.tmp dbus-bus-introspect.xml
#endif

#bin_PROGRAMS=dbus-send $(GLIB_TOOLS) dbus-launch dbus-cleanup-sockets $(GTK_TOOLS)

set (dbus_send_SOURCES
	../../tools/dbus-print-message.c			
	../../tools/dbus-print-message.h			
	../../tools/dbus-send.c
)

set (dbus_monitor_SOURCES
	../../tools/dbus-monitor.c				
	../../tools/dbus-print-message.c			
	../../tools/dbus-print-message.h
)

set (dbus_launch_SOURCES
	../../tools/dbus-launch.c
)

set (dbus_cleanup_sockets_SOURCES
	../../tools/dbus-cleanup-sockets.c
)

set (dbus_viewer_SOURCES
	../../tools/dbus-names-model.c			
	../../tools/dbus-names-model.h			
	../../tools/dbus-tree-view.c			
	../../tools/dbus-tree-view.h			
	../../tools/dbus-viewer.c
)

add_executable(dbus-send ${dbus_send_SOURCES})
target_link_libraries(dbus-send dbus-1)
install_targets(/bin dbus-send )

# glib required 
#add_executable(dbus_launch ${dbus_launch_SOURCES})

add_executable(dbus-monitor ${dbus_monitor_SOURCES})
target_link_libraries(dbus-monitor dbus-1)
install_targets(/bin dbus-monitor )

#dbus_send_LDADD= $(top_builddir)/dbus/libdbus-1.la
#dbus_monitor_LDADD= $(top_builddir)/glib/libdbus-glib-1.la
#dbus_launch_LDADD= $(DBUS_X_LIBS)
#dbus_viewer_LDADD= $(top_builddir)/glib/libdbus-gtool.la $(DBUS_GTK_THREADS_LIBS) $(DBUS_GLIB_TOOL_LIBS)

#man_MANS = dbus-send.1 dbus-monitor.1 dbus-launch.1 dbus-cleanup-sockets.1
#EXTRA_DIST = $(man_MANS) run-with-tmp-session-bus.sh
#CLEANFILES = 				
#	run-with-tmp-session-bus.conf	
#	dbus-bus-introspect.xml		
#	dbus-glib-bindings.h

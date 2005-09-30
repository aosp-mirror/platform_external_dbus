if HAVE_QT3
INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_QT3_CXXFLAGS)

dbusincludedir=$(includedir)/dbus-1.0/dbus

lib_LTLIBRARIES=libdbus-qt-1.la

dbusinclude_HEADERS=				\
	dbus-qt.h message.h connection.h        \
	server.h

libdbus_qt_1_la_SOURCES = 			\
	$(top_srcdir)/qt3/dbus-qthread.cpp \
	$(top_srcdir)/qt3/message.cpp \
	$(top_srcdir)/qt3/connection.cpp \
	$(top_srcdir)/qt3/integrator.cpp \
	$(top_srcdir)/qt3/server.cpp \
	$(top_srcdir)/qt3/connection.h \
	$(top_srcdir)/qt3/integrator.h \
	$(top_srcdir)/qt3/server.h


$(top_srcdir)/qt3/connection.cpp: connection.moc
$(top_srcdir)/qt3/integrator.cpp: integrator.moc
$(top_srcdir)/qt3/server.cpp: server.moc
$(top_srcdir)/qt3/connection.h: connection.moc
$(top_srcdir)/qt3/integrator.h: integrator.moc
$(top_srcdir)/qt3/server.h: server.moc

CLEANFILES=connection.moc integrator.moc server.moc

libdbus_qt_1_la_LIBADD= $(DBUS_QT3_LIBS) $(top_builddir)/dbus/libdbus-1.la
libdbus_qt_1_la_LDFLAGS= -version-info 1:0 -no-undefined

%.moc: %.h
	$(QT3_MOC) $< > $@
endif

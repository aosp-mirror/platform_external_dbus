if HAVE_QT
INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_QT_CXXFLAGS)

dbusincludedir=$(includedir)/dbus-1.0/dbus

lib_LTLIBRARIES=libdbus-qt-1.la

dbusinclude_HEADERS=				\
	dbus-qt.h message.h connection.h        \
	server.h

libdbus_qt_1_la_SOURCES = 			\
	dbus-qthread.cpp message.cpp connection.cpp \
	integrator.cpp server.cpp integrator.h

connection.cpp: connection.moc
integrator.cpp: integrator.moc
server.cpp: server.moc

CLEANFILES=connection.moc integrator.moc server.moc

libdbus_qt_1_la_LIBADD= $(DBUS_QT_LIBS) $(top_builddir)/dbus/libdbus-1.la
libdbus_qt_1_la_LDFLAGS= -version-info 1:0

%.moc: %.h
	$(QT_MOC) $< > $@
endif

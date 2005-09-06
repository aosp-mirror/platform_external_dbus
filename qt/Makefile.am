if HAVE_QT
INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_QT_CXXFLAGS)

dbusincludedir=$(includedir)/dbus-1.0/dbus

lib_LTLIBRARIES=libdbus-qt-1.la

dbusinclude_HEADERS=				\
	dbus-qt.h message.h connection.h        \
	server.h

libdbus_qt_1_la_SOURCES = 			\
	dbus-qthread.cpp \
	$(top_srcdir)/qt/message.cpp \
	$(top_srcdir)/qt/connection.cpp \
	$(top_srcdir)/qt/integrator.cpp \
	$(top_srcdir)/qt/server.cpp \
	$(top_srcdir)/qt/connection.h \
	$(top_srcdir)/qt/integrator.h \
	$(top_srcdir)/qt/server.h


$(top_srcdir)/qt/connection.cpp: connection.moc
$(top_srcdir)/qt/integrator.cpp: integrator.moc
$(top_srcdir)/qt/server.cpp: server.moc
$(top_srcdir)/qt/connection.h: connection.moc
$(top_srcdir)/qt/integrator.h: integrator.moc
$(top_srcdir)/qt/server.h: server.moc

CLEANFILES=connection.moc integrator.moc server.moc

libdbus_qt_1_la_LIBADD= $(DBUS_QT_LIBS) $(top_builddir)/dbus/libdbus-1.la
libdbus_qt_1_la_LDFLAGS= -version-info 1:0 -no-undefined

%.moc: %.h
	$(QT_MOC) $< > $@
endif

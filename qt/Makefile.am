if HAVE_QT
INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_QT_CFLAGS) -DDBUS_COMPILATION

dbusincludedir=$(includedir)/dbus-1.0/dbus

lib_LTLIBRARIES=libdbus-qt4-1.la

dbusinclude_HEADERS=	\
	qdbuserror.h \
	qdbusmessage.h \
	qdbusserver.h \
	qdbusconnection.h \
	qdbusvariant.h

libdbus_qt4_1_la_SOURCES = 			\
	$(top_srcdir)/qt/qdbusconnection.cpp    \
	$(top_srcdir)/qt/qdbuserror.cpp         \
	$(top_srcdir)/qt/qdbusintegrator.cpp    \
	$(top_srcdir)/qt/qdbusmarshall.cpp      \
	$(top_srcdir)/qt/qdbusmessage.cpp       \
	$(top_srcdir)/qt/qdbusserver.cpp        \
	$(top_srcdir)/qt/qdbusconnection.h      \
	$(top_srcdir)/qt/qdbuserror.h           \
	$(top_srcdir)/qt/qdbusmessage.h         \
	$(top_srcdir)/qt/qdbusserver.h          \
	$(top_srcdir)/qt/qdbusconnection_p.h    \
	$(top_srcdir)/dbus/qdbus.h              \
	$(top_srcdir)/qt/qdbusmarshall.h        \
	$(top_srcdir)/qt/qdbusmessage_p.h       \
	$(top_srcdir)/qt/qdbusvariant.h


$(top_srcdir)/qt/qdbusserver.cpp: qdbusserver.moc
$(top_srcdir)/qt/qdbusconnection.cpp: qdbusconnection.moc

CLEANFILES=qdbusserver.moc qdbusconnection.moc

libdbus_qt4_1_la_LIBADD= $(DBUS_QT_LIBS) $(top_builddir)/dbus/libdbus-1.la
libdbus_qt4_1_la_LDFLAGS= -version-info 1:0 -no-undefined

# _p.h files are a exception
qdbusconnection.moc: qdbusconnection_p.h
	$(QT_MOC) -o qdbusconnection.moc $(top_srcdir)/qt/qdbusconnection_p.h

%.moc: %.h
	$(QT_MOC) $< > $@
endif

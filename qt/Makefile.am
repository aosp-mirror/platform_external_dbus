if HAVE_QT
INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_QT_CFLAGS) -DDBUS_COMPILATION

dbusincludedir=$(includedir)/dbus-1.0/dbus

lib_LTLIBRARIES=libdbus-qt4-1.la

dbusinclude_HEADERS=	\
	qdbusmacros.h	\
	qdbuserror.h 	\
	qdbusmessage.h	\
	qdbusserver.h	\
	qdbusconnection.h \
	qdbusvariant.h	\
	qdbusobject.h	\
	qdbusinterface.h \
	qdbustype.h	\
	qdbusstandardinterfaces.h \
	qdbusutil.h	\
	qdbusintrospection.h \
	qdbusabstractadaptor.h

libdbus_qt4_1_la_SOURCES = 			\
	$(top_srcdir)/qt/qdbusconnection.cpp    \
	$(top_srcdir)/qt/qdbuserror.cpp         \
	$(top_srcdir)/qt/qdbusintegrator.cpp    \
	$(top_srcdir)/qt/qdbusmarshall.cpp      \
	$(top_srcdir)/qt/qdbusmessage.cpp       \
	$(top_srcdir)/qt/qdbusserver.cpp        \
	$(top_srcdir)/qt/qdbustype.cpp          \
	$(top_srcdir)/qt/qdbusobject.cpp        \
	$(top_srcdir)/qt/qdbusinterface.cpp     \
	$(top_srcdir)/qt/qdbusstandardinterfaces.cpp	\
	$(top_srcdir)/qt/qdbusxmlparser.cpp     \
	$(top_srcdir)/qt/qdbusutil.cpp          \
	$(top_srcdir)/qt/qdbusintrospection.cpp \
	$(top_srcdir)/qt/qdbusabstractadaptor.cpp	\
	$(top_srcdir)/qt/qdbusthread.cpp        \
						\
	$(top_srcdir)/dbus/qdbus.h              \
	$(top_srcdir)/qt/qdbusabstractadaptor.h \
	$(top_srcdir)/qt/qdbusconnection.h      \
	$(top_srcdir)/qt/qdbusconnection_p.h    \
	$(top_srcdir)/qt/qdbuserror.h           \
	$(top_srcdir)/qt/qdbusinterface.h       \
	$(top_srcdir)/qt/qdbusinterface_p.h     \
	$(top_srcdir)/qt/qdbusintrospection.h   \
	$(top_srcdir)/qt/qdbusmacros.h          \
	$(top_srcdir)/qt/qdbusmarshall.h        \
	$(top_srcdir)/qt/qdbusmessage.h         \
	$(top_srcdir)/qt/qdbusmessage_p.h       \
	$(top_srcdir)/qt/qdbusobject.h          \
	$(top_srcdir)/qt/qdbusobject_p.h        \
	$(top_srcdir)/qt/qdbusserver.h          \
	$(top_srcdir)/qt/qdbusstandardinterfaces.h \
	$(top_srcdir)/qt/qdbustype.h            \
	$(top_srcdir)/qt/qdbusvariant.h		\
	$(top_srcdir)/qt/qdbusxmlparser_p.h     


$(top_srcdir)/qt/qdbusabstractadaptor.lo: qdbusabstractadaptor.moc
$(top_srcdir)/qt/qdbusserver.lo: qdbusserver.moc
$(top_srcdir)/qt/qdbusconnection.lo: qdbusconnection_p.moc

CLEANFILES=qdbusabstractadaptor.moc qdbusserver.moc qdbusconnection.moc

libdbus_qt4_1_la_LIBADD= $(DBUS_QT_LIBS) $(top_builddir)/dbus/libdbus-1.la
libdbus_qt4_1_la_LDFLAGS= -version-info 1:0 -no-undefined

%.moc: %.h
	$(QT_MOC) $< > $@
endif

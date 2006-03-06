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
	qdbusabstractadaptor.h \
	qdbusreply.h

libdbus_qt4_1_la_SOURCES = 			\
	qdbusconnection.cpp    \
	qdbuserror.cpp         \
	qdbusintegrator.cpp    \
	qdbusinternalfilters.cpp \
	qdbusmarshall.cpp      \
	qdbusmessage.cpp       \
	qdbusserver.cpp        \
	qdbustype.cpp          \
	qdbusobject.cpp        \
	qdbusinterface.cpp     \
	qdbusstandardinterfaces.cpp	\
	qdbusxmlparser.cpp     \
	qdbusutil.cpp          \
	qdbusintrospection.cpp \
	qdbusabstractadaptor.cpp	\
	qdbusthread.cpp        \
						\
	qdbusabstractadaptor.h \
	qdbusabstractadaptor_p.h \
	qdbusconnection.h      \
	qdbusconnection_p.h    \
	qdbuserror.h           \
	qdbusinterface.h       \
	qdbusinterface_p.h     \
	qdbusintrospection.h   \
	qdbusmacros.h          \
	qdbusmarshall_p.h      \
	qdbusmessage.h         \
	qdbusmessage_p.h       \
	qdbusobject.h          \
	qdbusobject_p.h        \
	qdbusreply.h	       \
	qdbusserver.h          \
	qdbusstandardinterfaces.h \
	qdbustype.h            \
	qdbusvariant.h		\
	qdbusxmlparser_p.h     


qdbusabstractadaptor.lo: qdbusabstractadaptor.moc qdbusabstractadaptor_p.moc
qdbusserver.lo: qdbusserver.moc
qdbusconnection.lo: qdbusconnection_p.moc

CLEANFILES=qdbusabstractadaptor.moc qdbusserver.moc qdbusconnection.moc qdbusabstractadaptor_p.moc

libdbus_qt4_1_la_LIBADD= $(DBUS_QT_LIBS) $(top_builddir)/dbus/libdbus-1.la
libdbus_qt4_1_la_LDFLAGS= -version-info 1:0 -no-undefined

EXTRA_DIST = Doxyfile

%.moc: %.h
	$(QT_MOC) $< > $@
endif

INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_QT_CFLAGS) -DDBUS_COMPILATION

lib_LTLIBRARIES=libdbus-qt4-1.la

dbusincludedir=$(includedir)/dbus-1.0/dbus
dbusinclude_HEADERS=    \
        qdbusbus.h      \
        qdbusmacros.h   \
        qdbuserror.h    \
        qdbusmessage.h  \
        qdbusserver.h   \
        qdbusconnection.h \
        qdbusabstractinterface.h \
        qdbusinterface.h \
        qdbusutil.h     \
        qdbusabstractadaptor.h \
        qdbusreply.h    \
        qdbustypehelper_p.h

noinst_HEADERS=		\
	qdbusabstractadaptor_p.h \
	qdbusabstractinterface_p.h \
	qdbusconnection_p.h    \
	qdbusinterface_p.h     \
	qdbusintrospection_p.h \
	qdbusmarshall_p.h      \
	qdbusmessage_p.h       \
	qdbusmetaobject_p.h    \
	qdbustype_p.h		\
	qdbusxmlparser_p.h

MOCS = qdbusabstractadaptor.moc qdbusserver.moc qdbusconnection_p.moc qdbusconnection_p.moc qdbusabstractadaptor_p.moc qdbusbus.moc qdbusabstractinterface.moc
CLEANFILES = $(MOCS)
BUILT_SOURCES = $(MOCS)

libdbus_qt4_1_la_SOURCES = \
        qdbusbus.cpp            \
        qdbusconnection.cpp     \
        qdbuserror.cpp          \
        qdbusintegrator.cpp     \
        qdbusmarshall.cpp       \
        qdbusmessage.cpp        \
        qdbusserver.cpp         \
        qdbustype.cpp           \
        qdbusabstractinterface.cpp	\
        qdbusinterface.cpp      \
        qdbusxmlparser.cpp      \
        qdbusutil.cpp           \
        qdbusintrospection.cpp  \
        qdbusabstractadaptor.cpp	\
        qdbusthread.cpp         \
        qdbusinternalfilters.cpp	\
        qdbusmetaobject.cpp	\
	qdbusmisc.cpp		\
	qdbusxmlgenerator.cpp

qdbusabstractadaptor.lo: qdbusabstractadaptor.moc qdbusabstractadaptor_p.moc
qdbusabstractinterface.lo: qdbusabstractinterface.moc
qdbusbus.lo: qdbusbus.moc
qdbusserver.lo: qdbusserver.moc
qdbusintegrator.lo: qdbusconnection_p.moc

libdbus_qt4_1_la_LIBADD= $(DBUS_QT_LIBS) $(top_builddir)/dbus/libdbus-1.la
libdbus_qt4_1_la_LDFLAGS= -version-info 1:0 -no-undefined
libdbus_qt4_1_la_CPPFLAGS= -DQDBUS_MAKEDLL

%.moc: %.h
	$(QT_MOC) $< > $@

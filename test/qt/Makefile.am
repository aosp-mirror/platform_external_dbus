INCLUDES=-I$(top_srcdir) -I$(top_srcdir)/qt $(DBUS_CLIENT_CFLAGS) $(DBUS_QT_CFLAGS) $(DBUS_QTESTLIB_CFLAGS) -DDBUS_COMPILATION

if DBUS_BUILD_TESTS
TEST_BINARIES=qdbusconnection qdbusobject qdbusinterface qdbustype qdbusxmlparser hal
TESTS=
else
TEST_BINARIES=
TESTS=
endif


noinst_PROGRAMS= $(TEST_BINARIES)

qdbusconnection_SOURCES= tst_qdbusconnection.cpp
qdbusobject_SOURCES= tst_qdbusobject.cpp
qdbusinterface_SOURCES= tst_qdbusinterface.cpp
qdbustype_SOURCES= tst_qdbustype.cpp
qdbusxmlparser_SOURCES= tst_qdbusxmlparser.cpp
hal_SOURCES = tst_hal.cpp

tst_qdbusconnection.o: tst_qdbusconnection.moc
tst_qdbusobject.o: tst_qdbusobject.moc
tst_qdbusinterface.o: tst_qdbusinterface.moc
tst_qdbustype.o: tst_qdbustype.moc
tst_qdbusxmlparser.o: tst_qdbusxmlparser.moc
tst_hal.o: tst_hal.moc

%.moc: %.cpp
	$(QT_MOC) $< > $@

TEST_LIBS=$(DBUS_QTESTLIB_LIBS) $(top_builddir)/qt/libdbus-qt4-1.la

LDADD=$(TEST_LIBS)

CLEANFILES=tst_qdbusconnection.moc

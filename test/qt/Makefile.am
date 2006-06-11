INCLUDES=-I$(top_srcdir) -I$(top_srcdir)/qt $(DBUS_CLIENT_CFLAGS) $(DBUS_QT_CFLAGS) $(DBUS_QTESTLIB_CFLAGS) -DDBUS_COMPILATION

if DBUS_BUILD_TESTS
TEST_BINARIES = tst_headertest tst_qdbusxmlparser tst_qdbusconnection qpong tst_qdbusmarshall tst_qdbusinterface tst_qdbusabstractadaptor tst_hal
TESTS=
else
TEST_BINARIES=
TESTS=
endif

noinst_PROGRAMS= $(TEST_BINARIES)

qpong_SOURCES= qpong.cpp
tst_headertest_SOURCES = tst_headertest.cpp
tst_qdbusconnection_SOURCES = tst_qdbusconnection.cpp
tst_qdbusxmlparser_SOURCES = tst_qdbusxmlparser.cpp
tst_qdbusmarshall_SOURCES = tst_qdbusmarshall.cpp
tst_qdbusinterface_SOURCES = tst_qdbusinterface.cpp
tst_qdbusabstractadaptor_SOURCES = tst_qdbusabstractadaptor.cpp common.h
tst_hal_SOURCES = tst_hal.cpp

qpong.o: qpong.moc
tst_qdbusxmlparser.o: tst_qdbusxmlparser.moc
tst_qdbusmarshall.o: tst_qdbusmarshall.moc
tst_qdbusconnection.o: tst_qdbusconnection.moc
tst_qdbusinterface.o: tst_qdbusinterface.moc
tst_qdbusabstractadaptor.o: tst_qdbusabstractadaptor.moc
tst_hal.o: tst_hal.moc

%.moc: %.cpp
	$(QT_MOC) $< > $@

TEST_LIBS=$(DBUS_QTESTLIB_LIBS) $(top_builddir)/qt/src/libdbus-qt4-1.la

LDADD=$(TEST_LIBS)

clean-local:
	-rm *.moc

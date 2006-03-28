INCLUDES=-I$(top_srcdir) -I$(top_srcdir)/qt $(DBUS_CLIENT_CFLAGS) $(DBUS_QT_CFLAGS) $(DBUS_QTESTLIB_CFLAGS) -DDBUS_COMPILATION

if DBUS_BUILD_TESTS
TEST_BINARIES=qdbusconnection qpong ping qdbusinterface qdbusabstractadaptor hal
TESTS=
else
TEST_BINARIES=
TESTS=
endif

noinst_PROGRAMS= $(TEST_BINARIES)

qpong_SOURCES= qpong.cpp
ping_SOURCES= ping.cpp
qdbusconnection_SOURCES= tst_qdbusconnection.cpp
qdbusinterface_SOURCES= tst_qdbusinterface.cpp
qdbusabstractadaptor_SOURCES= tst_qdbusabstractadaptor.cpp common.h
hal_SOURCES = tst_hal.cpp

qpong.o: qpong.moc
ping.o: ping.moc
tst_qdbusconnection.o: tst_qdbusconnection.moc
tst_qdbusinterface.o: tst_qdbusinterface.moc
tst_qdbusabstractadaptor.o: tst_qdbusabstractadaptor.moc
tst_hal.o: tst_hal.moc

%.moc: %.cpp
	$(QT_MOC) $< > $@

TEST_LIBS=$(DBUS_QTESTLIB_LIBS) $(top_builddir)/qt/libdbus-qt4-1.la

LDADD=$(TEST_LIBS)

clean-local:
	-rm *.moc

INCLUDES=-I$(top_srcdir) -I$(top_srcdir)/qt $(DBUS_CLIENT_CFLAGS) $(DBUS_QT_CFLAGS) $(DBUS_QTESTLIB_CFLAGS) -DDBUS_COMPILATION

if DBUS_BUILD_TESTS
TEST_BINARIES=qdbusconnection
TESTS=
else
TEST_BINARIES=
TESTS=
endif


noinst_PROGRAMS= $(TEST_BINARIES)

qdbusconnection_SOURCES= \
	tst_qdbusconnection.cpp

tst_qdbusconnection.cpp: tst_qdbusconnection.moc

tst_qdbusconnection.moc:
	$(QT_MOC) $(srcdir)/tst_qdbusconnection.cpp > tst_qdbusconnection.moc

TEST_LIBS=$(DBUS_QTESTLIB_LIBS) $(top_builddir)/qt/libdbus-qt4-1.la

qdbusconnection_LDADD=$(TEST_LIBS)

CLEANFILES=tst_qdbusconnection.moc

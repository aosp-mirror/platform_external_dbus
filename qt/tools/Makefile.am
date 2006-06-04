INCLUDES=-I$(top_srcdir)/qt $(DBUS_CLIENT_CFLAGS) $(DBUS_QT_CFLAGS) -DDBUS_COMPILATION
bin_PROGRAMS = dbusidl2cpp dbuscpp2xml

dbusidl2cpp_SOURCES = dbusidl2cpp.cpp
dbusidl2cpp_LDFLAGS = -no-undefined 
dbusidl2cpp_LDADD = $(DBUS_QT_LIBS) ../src/libdbus-qt4-1.la

dbuscpp2xml_SOURCES = dbuscpp2xml.cpp
dbuscpp2xml_LDFLAGS = -no-undefined 
dbuscpp2xml_LDADD = $(DBUS_QT_LIBS) ../src/libdbus-qt4-1.la


INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS)

dbusincludedir=$(includedir)/dbus-1.0/dbus

lib_LTLIBRARIES=libdbus-1.la

dbusinclude_HEADERS=				\
	dbus.h

libdbus_1_la_SOURCES=				\
	dbus-message.c

noinst_LTLIBRARIES=libdbus-convenience.la

libdbus_convenience_la_SOURCES=			\
	dbus-hash.c

libdbus_1_la_LIBADD=  $(DBUS_CLIENT_LIBS) libdbus-convenience.la
## don't export symbols that start with "_" (we use this 
## convention for internal symbols)
libdbus_1_la_LDFLAGS= -export-symbols-regex "^[[^_]].*"

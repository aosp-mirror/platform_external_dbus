SUBDIRS=org
JAR=jar

CLEANFILES=dbus-1.jar

dbus-1.jar: 
	-@rm -f dbus-1.jar
	find org -type d -o -type f -name '*.class' | \
	sed -e '/\/\./d' | \
	$(JAR) cfM0E@ $@

jardir = $(datadir)/java
jar_DATA = dbus-1.jar

noinst_PROGRAMS = test-message

test_message_SOURCES = TestMessage.java
test_message_LDFLAGS = --main=TestMessage
test_message_LDADD = $(top_builddir)/gcj/org/freedesktop/dbus/libdbus-gcj-1.la

DESTDIR=

DLLS=dbus-sharp.dll
NOINST_EXES=test-dbus-sharp

DBUS_SHARP_FILES= DBus.cs Message.cs Connection.cs Error.cs
TEST_DBUS_SHARP_FILES=Test.cs

all: $(DLLS) $(NOINST_EXES)

dbus-sharp.dll: $(DBUS_SHARP_FILES)
	$(MCS) $(MCSFLAGS) --unsafe --target library -o dbus-sharp.dll $(DBUS_SHARP_FILES)

test-dbus-sharp: $(TEST_DBUS_SHARP_FILES)
	$(MCS) $(MCSFLAGS) --unsafe --target exe -L . -r dbus-sharp.dll -o test-dbus-sharp $(TEST_DBUS_SHARP_FILES)

clean:
	rm -f $(DLLS) $(NOINST_EXES)

install: all
	../mkinstalldirs $(DESTDIR)$(prefix)/lib && \
	cp $(DLLS) $(DESTDIR)$(prefix)/lib || exit 1

EXTRA_DIST=$(DBUS_SHARP_FILES)


DESTDIR=

DLLS=dbus-sharp.dll
NOINST_EXES=test-dbus-sharp.exe

DBUS_SHARP_FILES= 		\
	Arguments.cs		\
	Bus.cs			\
	Connection.cs		\
	Custom.cs		\
	DBusException.cs	\
	Error.cs		\
	Handler.cs		\
	InterfaceAttribute.cs	\
	InterfaceProxy.cs	\
	Introspector.cs		\
	Message.cs		\
	MethodAttribute.cs	\
	MethodCall.cs		\
	MethodReturn.cs		\
	ProxyBuilder.cs		\
	Server.cs		\
	Service.cs		\
	Signal.cs		\
	DBusType/IDBusType.cs	\
	DBusType/Array.cs	\
	DBusType/Boolean.cs	\
	DBusType/Byte.cs	\
	DBusType/Custom.cs	\
	DBusType/Dict.cs	\
	DBusType/Double.cs	\
	DBusType/Int32.cs	\
	DBusType/Int64.cs	\
	DBusType/Nil.cs		\
	DBusType/ObjectPath.cs	\
	DBusType/String.cs	\
	DBusType/UInt32.cs	\
	DBusType/UInt64.cs

TEST_DBUS_SHARP_FILES=Test.cs

all: $(DLLS) $(NOINST_EXES)

dbus-sharp.dll: $(DBUS_SHARP_FILES)
	$(MCS) $(MCSFLAGS) --unsafe --target library -o dbus-sharp.dll $(DBUS_SHARP_FILES)

test-dbus-sharp.exe: $(TEST_DBUS_SHARP_FILES)
	$(MCS) $(MCSFLAGS) --unsafe --target exe -L . -r dbus-sharp.dll -r gtk-sharp.dll -o test-dbus-sharp.exe $(TEST_DBUS_SHARP_FILES)

clean:
	rm -f $(DLLS) $(NOINST_EXES)

install: all
	../mkinstalldirs $(DESTDIR)$(prefix)/lib && \
	cp $(DLLS) $(DESTDIR)$(prefix)/lib || exit 1

EXTRA_DIST=$(DBUS_SHARP_FILES)


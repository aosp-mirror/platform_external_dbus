DESTDIR=

SUBDIRS=. doc example

TARGET=$(ASSEMBLY)
NOINST_EXES=test-dbus-sharp.exe
ASSEMBLY_NAME=dbus-sharp

DBUS_SHARP_FILES= 		\
	Arguments.cs		\
	Bus.cs			\
	Connection.cs		\
	Custom.cs		\
	DBusException.cs	\
	Error.cs		\
	ErrorMessage.cs		\
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
	SignalAttribute.cs	\
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

ASSEMBLY=$(ASSEMBLY_NAME).dll

DISTCLEANFILES=AssemblyInfo.cs $(ASSEMBLY).config

all-am: $(TARGET)

dbus-sharp.dll: $(DBUS_SHARP_FILES)
	$(CSC) --unsafe --target library -o dbus-sharp.dll $(DBUS_SHARP_FILES) AssemblyInfo.cs

test-dbus-sharp.exe: $(TEST_DBUS_SHARP_FILES)
	$(CSC) --unsafe --target exe -L . -r dbus-sharp.dll -pkg:gtk-sharp -o test-dbus-sharp.exe $(TEST_DBUS_SHARP_FILES)

clean:
	rm -f $(TARGET) $(NOINST_EXES)

install-data-local:
	@if test -n '$(TARGET)'; then                       \
	  echo "$(GACUTIL) /i $(ASSEMBLY) /f /package dbus-sharp /gacdir $(DESTDIR)$(libdir)";    \
	  $(GACUTIL) /i $(ASSEMBLY) /f /package dbus-sharp /gacdir $(DESTDIR)$(libdir) || exit 1; \
	fi

uninstall-local:
	@if test -n '$(TARGET)'; then                       \
	  echo "$(GACUTIL) /u $(ASSEMBLY_NAME) /package dbus-sharp /gacdir $(DESTDIR)$(libdir)";  \
	  $(GACUTIL) /u $(ASSEMBLY_NAME) /package dbus-sharp /gacdir $(DESTDIR)$(libdir) || exit 1;   \
	fi

EXTRA_DIST=			\
	$(DBUS_SHARP_FILES)	\
	AssemblyInfo.cs.in	\
	$(ASSEMBLY).config.in   \
	dbus-sharp.snk

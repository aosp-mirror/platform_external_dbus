SUBDIRS=. doc example

TARGET=$(ASSEMBLY)
NOINST_EXES=test-dbus-sharp.exe
ASSEMBLY_NAME=dbus-sharp
GACUTIL=gacutil

DBUS_SHARP_FILES= 				\
	$(srcdir)/Arguments.cs			\
	$(srcdir)/Bus.cs			\
	$(srcdir)/BusDriver.cs			\
	$(srcdir)/Connection.cs			\
	$(srcdir)/Custom.cs			\
	$(srcdir)/DBusException.cs		\
	$(srcdir)/Error.cs			\
	$(srcdir)/ErrorMessage.cs		\
	$(srcdir)/Handler.cs			\
	$(srcdir)/InterfaceAttribute.cs		\
	$(srcdir)/InterfaceProxy.cs		\
	$(srcdir)/Introspector.cs		\
	$(srcdir)/Message.cs			\
	$(srcdir)/MethodAttribute.cs		\
	$(srcdir)/MethodCall.cs			\
	$(srcdir)/MethodReturn.cs		\
	$(srcdir)/ProxyBuilder.cs		\
	$(srcdir)/Server.cs			\
	$(srcdir)/Service.cs			\
	$(srcdir)/Signal.cs			\
	$(srcdir)/SignalAttribute.cs		\
	$(srcdir)/DBusType/IDBusType.cs		\
	$(srcdir)/DBusType/Array.cs		\
	$(srcdir)/DBusType/Boolean.cs		\
	$(srcdir)/DBusType/Byte.cs		\
	$(srcdir)/DBusType/Custom.cs		\
	$(srcdir)/DBusType/Dict.cs		\
	$(srcdir)/DBusType/Double.cs		\
	$(srcdir)/DBusType/Int32.cs		\
	$(srcdir)/DBusType/Int64.cs		\
	$(srcdir)/DBusType/Nil.cs		\
	$(srcdir)/DBusType/ObjectPath.cs	\
	$(srcdir)/DBusType/String.cs		\
	$(srcdir)/DBusType/UInt32.cs		\
	$(srcdir)/DBusType/UInt64.cs

TEST_DBUS_SHARP_FILES=Test.cs

ASSEMBLY=$(ASSEMBLY_NAME).dll

DISTCLEANFILES=AssemblyInfo.cs $(ASSEMBLY).config $(ASSEMBLY)

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
	  $(GACUTIL) /i $(ASSEMBLY) /f /package dbus-sharp /gacdir $(libdir) /root $(DESTDIR)$(libdir) || exit 1; \
	fi

uninstall-local:
	@if test -n '$(TARGET)'; then                       \
	  echo "$(GACUTIL) /u $(ASSEMBLY_NAME) /package dbus-sharp /gacdir $(DESTDIR)$(libdir)";  \
	  $(GACUTIL) /u $(ASSEMBLY_NAME) /package dbus-sharp /gacdir $(libdir) /root $(DESTDIR)$(libdir) || exit 1;   \
	fi

EXTRA_DIST=			\
	$(DBUS_SHARP_FILES)	\
	AssemblyInfo.cs.in	\
	$(ASSEMBLY).config.in   \
	dbus-sharp.snk

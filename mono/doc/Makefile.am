if ENABLE_MONODOC
TARGETS = dbus-sharp-docs.zip dbus-sharp-docs.tree dbus-sharp-docs.source
DOCDIR = `$(MONODOC) --get-sourcesdir`

install-data-local:
	install -m 644 $(srcdir)/dbus-sharp-docs.source `monodoc --get-sourcesdir`
	install -m 644 $(srcdir)/dbus-sharp-docs.tree `monodoc --get-sourcesdir`
	install -m 644 $(srcdir)/dbus-sharp-docs.zip `monodoc --get-sourcesdir`
else
TARGETS = 
DOCDIR = $(datadir)
endif

noinst_DATA = $(TARGETS)

ASSEMBLER = $(MONODOC) --assemble
UPDATER = $(MONODOC) --update

dbus-sharp-docs.zip dbus-sharp-docs.tree: $(srcdir)/en/*/*.xml $(srcdir)/en/*.xml
	$(ASSEMBLER) --ecma $(srcdir)/en -o dbus-sharp-docs

update:
		$(UPDATER) ../dbus-sharp.dll -o ./en -f

CLEANFILES = dbus-sharp-docs.zip dbus-sharp-docs.tree

EXTRA_DIST = dbus-sharp-docs.source dbus-sharp-docs.tree dbus-sharp-docs.zip

dist-hook:
	mkdir -p $(distdir)/en
	cp $(srcdir)/en/*.xml $(distdir)/en/
	mkdir -p $(distdir)/en/DBus
	cp $(srcdir)/en/DBus/*.xml $(distdir)/en/DBus.DBusType/
	mkdir -p $(distdir)/en/DBus.DBusType
	cp $(srcdir)/en/DBus/*.xml $(distdir)/en/DBus.DBusType/


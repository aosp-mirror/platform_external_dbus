if ENABLE_MONODOC
TARGETS = dbus-sharp-docs.zip dbus-sharp-docs.tree dbus-sharp-docs.source
DOCDIR = `$(MONODOC) --get-sourcesdir`

install-data-local:
	install -d -m 755 $(DESTDIR)/`monodoc --get-sourcesdir`
	install -m 644 $(srcdir)/dbus-sharp-docs.source $(DESTDIR)/`monodoc --get-sourcesdir`
	install -m 644 $(srcdir)/dbus-sharp-docs.tree $(DESTDIR)/`monodoc --get-sourcesdir`
	install -m 644 $(srcdir)/dbus-sharp-docs.zip $(DESTDIR)/`monodoc --get-sourcesdir`
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

EXTRA_DIST = dbus-sharp-docs.source
if ENABLE_MONODOC
EXTRA_DIST += dbus-sharp-docs.tree dbus-sharp-docs.zip
endif

dist-hook:
	mkdir -p $(distdir)/en
	cp $(srcdir)/en/*.xml $(distdir)/en/
	mkdir -p $(distdir)/en/DBus
	cp $(srcdir)/en/DBus/*.xml $(distdir)/en/DBus/
	mkdir -p $(distdir)/en/DBus.DBusType
	cp $(srcdir)/en/DBus.DBusType/*.xml $(distdir)/en/DBus.DBusType/



if HAVE_GLIB
    GLIB_SUBDIR=glib
    GLIB_PC=dbus-glib-1.0.pc
endif

SUBDIRS=dbus $(GLIB_SUBDIR) bus test doc 

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = dbus-1.0.pc $(GLIB_PC)

DISTCLEANFILES = 		\
	dbus-1.0.pc		\
	$(GLIB_PC)

EXTRA_DIST =			\
	dbus-1.0.pc.in		\
	dbus-glib-1.0.pc.in

all-local: Doxyfile


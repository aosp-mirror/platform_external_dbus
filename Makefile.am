
if HAVE_GLIB
    GLIB_SUBDIR=glib
    GLIB_PC=dbus-glib-1.0.pc
endif

if HAVE_QT
   QT_SUBDIR=qt
endif

SUBDIRS=dbus bus test doc $(GLIB_SUBDIR) $(QT_SUBDIR) 

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = dbus-1.0.pc $(GLIB_PC)

DISTCLEANFILES = 		\
	dbus-1.0.pc		\
	$(GLIB_PC)

EXTRA_DIST =			\
	HACKING			\
	dbus-1.0.pc.in		\
	dbus-glib-1.0.pc.in

all-local: Doxyfile



if HAVE_GLIB
    GLIB_SUBDIR=glib
    GLIB_PC=dbus-glib-1.pc
endif

if HAVE_QT
   QT_SUBDIR=qt
endif

if DBUS_USE_GCJ
   GCJ_SUBDIR=gcj
endif

if DBUS_USE_MCS
   MONO_SUBDIR=mono
endif

if HAVE_PYTHON
   PYTHON_SUBDIR=python
endif

SUBDIRS=dbus bus doc $(GLIB_SUBDIR) $(GCJ_SUBDIR) $(MONO_SUBDIR) $(QT_SUBDIR) $(PYTHON_SUBDIR) test tools

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = dbus-1.pc $(GLIB_PC)

DISTCLEANFILES = 		\
	dbus-1.pc		\
	$(GLIB_PC)

EXTRA_DIST =			\
	HACKING			\
	dbus-1.pc.in		\
	dbus-glib-1.pc.in

all-local: Doxyfile

if DBUS_GCOV_ENABLED
clean-gcov:
	find -name "*.da" -o -name "*.gcov" | xargs rm || true

## .PHONY so it always rebuilds it
.PHONY: coverage-report.txt
coverage-report.txt:
	GCOV_FILES=`find dbus bus $(GLIB_SUBDIR) $(QT_SUBDIR) -name "*.bbg"` ;		\
	C_FILES= ;									\
	for F in $$GCOV_FILES; do							\
		C=`echo $$F | sed -e 's/.bbg/.c/g'` ;					\
		DA=`echo $$F | sed -e 's/.bbg/.da/g'` ;					\
		if test -e $$DA ; then							\
			C_FILES="$$C_FILES $$C" ;					\
		fi ;									\
	done ;										\
	echo $$C_FILES ;								\
	$(top_builddir)/test/decode-gcov --report $$C_FILES > coverage-report.txt

check-coverage: clean-gcov all check coverage-report.txt
	cat coverage-report.txt

else
coverage-report.txt:
	echo "Need to reconfigure with --enable-gcov"

check-coverage:
	echo "Need to reconfigure with --enable-gcov"

endif


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

if DBUS_GCOV_ENABLED
clean-gcov:
	find -name "*.da" -o -name "*.gcov" | xargs rm || true

## .PHONY so it always rebuilds it
.PHONY: coverage-report.txt
coverage-report.txt:
	GCOV_FILES=`find -name "*.bbg"` ;					\
	C_FILES= ;								\
	for F in $$GCOV_FILES; do						\
		C=`echo $$F | sed -e 's/.bbg/.c/g'` ;				\
		DA=`echo $$F | sed -e 's/.bbg/.da/g'` ;				\
		if test -e $$DA ; then						\
			C_FILES="$$C_FILES $$C" ;				\
		fi ;								\
	done ;									\
	echo $$C_FILES ; 							\
	$(top_builddir)/test/decode-gcov --report $$C_FILES > coverage-report.txt

check-coverage: clean-gcov all check coverage-report.txt
	cat coverage-report.txt

else
coverage-report.txt:
	echo "Need to reconfigure with --enable-gcov"

check-coverage:
	echo "Need to reconfigure with --enable-gcov"

endif

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

if DBUS_USE_CSC
   MONO_SUBDIR=mono
   MONO_PC=dbus-sharp.pc
endif

if HAVE_PYTHON
   PYTHON_SUBDIR=python
endif

## really we should require gcj/mcs/python also but since they are 
## annoying to install, we don't for now
dist-local:
	if test -z "$(QT_SUBDIR)" || test -z "$(GLIB_SUBDIR)" ; then		\
		echo "You have to build with Qt and GLib to make dist" ;	\
	fi 

SUBDIRS=dbus bus doc $(GLIB_SUBDIR) $(GCJ_SUBDIR) $(MONO_SUBDIR) $(QT_SUBDIR) $(PYTHON_SUBDIR) test tools
DIST_SUBDIRS=dbus bus doc glib qt gcj mono python test tools

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = dbus-1.pc $(GLIB_PC) $(MONO_PC)

DISTCLEANFILES = 		\
	dbus-1.pc		\
	$(GLIB_PC)		\
	$(MONO_PC)

EXTRA_DIST =			\
	HACKING			\
	dbus-1.pc.in		\
	dbus-glib-1.pc.in	\
	dbus-sharp.pc.in

all-local: Doxyfile

if DBUS_GCOV_ENABLED
clean-gcov:
	find -name "*.da" -o -name "*.gcov" | xargs rm || true

clean-bbg:
	find -name "*.bbg" -o -name "*.bb" | xargs rm || true

GCOV_DIRS=dbus bus $(GLIB_SUBDIR) $(QT_SUBDIR)

## .PHONY so it always rebuilds it
.PHONY: coverage-report.txt
coverage-report.txt:
	BBG_FILES=`find $(GCOV_DIRS) -name "*.bbg" -o -name "*.gcno"` ;			\
	C_FILES= ;									\
	for F in $$BBG_FILES ; do							\
		F_nolibs=`echo $$F | sed -e 's/.libs\///g'` ;				\
		C=`echo $$F_nolibs | sed -e 's/.bbg/.c/g' | sed -e 's/.gcno/.c/g'`  ;	\
		B=`basename $$F .bbg` ;							\
		D=`dirname $$F` ;							\
		DA=`echo $$F | sed -e 's/.bbg/.da/g'` ;					\
		DA_libs=`echo $$D/.libs/$$B/.da` ;					\
		if test -e $$DA || test -e $$DA_libs; then				\
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

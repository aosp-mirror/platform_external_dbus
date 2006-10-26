INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) $(DBUS_GLIB_CFLAGS) -DDBUS_COMPILATION

## note that TESTS has special meaning (stuff to use in make check)
## so if adding tests not to be run in make check, don't add them to 
## TESTS
if DBUS_BUILD_TESTS
TESTS_ENVIRONMENT=DBUS_TOP_BUILDDIR=@abs_top_builddir@ DBUS_TOP_SRCDIR=@abs_top_srcdir@
TESTS=run-test.sh
else
TESTS=
endif

EXTRA_DIST=run-test.sh 

if DBUS_BUILD_TESTS

## we use noinst_PROGRAMS not check_PROGRAMS for TESTS so that we
## build even when not doing "make check"
noinst_PROGRAMS=test-names test-pending-call-dispatch test-threads-init

test_names_SOURCES=				\
	test-names.c

test_names_LDADD=$(top_builddir)/dbus/libdbus-convenience.la $(DBUS_TEST_LIBS)
test_names_LDFLAGS=@R_DYNAMIC_LDFLAG@

test_pending_call_dispatch_SOURCES =		\
	test-pending-call-dispatch.c

test_pending_call_dispatch_LDADD=$(top_builddir)/dbus/libdbus-convenience.la $(DBUS_TEST_LIBS)
test_pending_call_dispatch_LDFLAGS=@R_DYNAMIC_LDFLAG@

test_threads_init_SOURCES =            \
	test-threads-init.c

test_threads_init_LDADD=$(top_builddir)/dbus/libdbus-convenience.la $(DBUS_TEST_LIBS)
test_threads_init_LDFLAGS=@R_DYNAMIC_LDFLAG@

endif


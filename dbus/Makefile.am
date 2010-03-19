
configdir=$(sysconfdir)/dbus-1

INCLUDES=-I$(top_builddir) -I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) @PIC_CFLAGS@ -DDBUS_COMPILATION	\
	-DDBUS_MACHINE_UUID_FILE=\""$(localstatedir)/lib/dbus/machine-id"\"		\
	-DDBUS_SYSTEM_CONFIG_FILE=\""$(configdir)/system.conf"\"			\
	-DDBUS_SESSION_CONFIG_FILE=\""$(configdir)/session.conf"\"

dbusincludedir=$(includedir)/dbus-1.0/dbus
dbusarchincludedir=$(libdir)/dbus-1.0/include/dbus

lib_LTLIBRARIES=libdbus-1.la

#
# Deal with W32 .def and version-info.rc stuff
#
if DBUS_WIN

built_sources_sys = dbus-1.def dbus-convenience.def

# Note that -P suppresses line markers.
PREPROCESS = $(CPP) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) -P
dbus-1.def: $(srcdir)/dbus-1.def.pre
	$(PREPROCESS) - < $< > $@

dbus-convenience.def: $(srcdir)/dbus-convenience.def.pre
	$(PREPROCESS) - < $< > $@

SUFFIXES = rc pre

.rc.o:
	$(WINDRES) $< -o $@

dbus_res = versioninfo.o
dbus_res_ldflag = -Wl,$(dbus_res)
no_undefined = -no-undefined


### FIXME use the .def.in files depending on the current config 
### or combine them in a single .def file as done in the cmake build
export_symbols = -export-symbols dbus-1.def
export_symbols_convenience = -export-symbols dbus-convenience.def

install-def-file:
	test -z "$(libdir)" || $(MKDIR_P) "$(DESTDIR)$(libdir)"
	$(INSTALL) dbus-1.def $(DESTDIR)$(libdir)/dbus-1.def

uninstall-def-file:
	-rm $(DESTDIR)$(libdir)/dbus-1.def

libdbus_1_la_DEPENDENCIES = $(dbus_res) dbus-1.def
intllibs =

else
built_sources_sys =
dbus_res =
dbus_res_ldflag =
no_undefined =
## don't export symbols that start with "_" (we use this
## convention for internal symbols)
export_symbols = -export-symbols-regex "^[^_].*"
export_symbols_convenience =
install-def-file:
uninstall-def-file:

intllibs = @LTLIBINTL@

endif


#
# Platform-dependent sources:
#
if DBUS_WIN
DBUS_LIB_arch_sources = 			\
	dbus-server-win.c 			\
	dbus-server-win.h

DBUS_SHARED_arch_sources = 			\
	dbus-file-win.c 			\
	dbus-pipe-win.c 			\
	dbus-sockets-win.h			\
	dbus-sysdeps-win.c 			\
	dbus-sysdeps-win.h			\
	dbus-sysdeps-thread-win.c		\
	dbus-transport-win.c			\
	dbus-transport-win.h

DBUS_UTIL_arch_sources =			\
	dbus-sysdeps-util-win.c			\
	dbus-spawn-win.c
else
DBUS_LIB_arch_sources = 			\
	dbus-uuidgen.c				\
	dbus-uuidgen.h				\
	dbus-server-unix.c 			\
	dbus-server-unix.h

DBUS_SHARED_arch_sources = 			\
	dbus-file-unix.c 			\
	dbus-pipe-unix.c 			\
	dbus-sysdeps-unix.c 			\
	dbus-sysdeps-unix.h			\
	dbus-sysdeps-pthread.c			\
	dbus-transport-unix.c			\
	dbus-transport-unix.h			\
	dbus-userdb.c				\
	dbus-userdb.h

DBUS_UTIL_arch_sources = 			\
	dbus-sysdeps-util-unix.c		\
	dbus-userdb-util.c			\
	dbus-spawn.c
endif

dbusinclude_HEADERS=				\
	dbus.h					\
	dbus-address.h				\
	dbus-bus.h				\
	dbus-connection.h			\
	dbus-errors.h				\
	dbus-macros.h				\
	dbus-memory.h				\
	dbus-message.h				\
	dbus-misc.h				\
	dbus-pending-call.h			\
	dbus-protocol.h				\
	dbus-server.h				\
	dbus-shared.h				\
	dbus-signature.h			\
	dbus-threads.h				\
	dbus-types.h


dbusarchinclude_HEADERS=			\
	dbus-arch-deps.h

### source code that goes in the installed client library
### and is specific to library functionality
DBUS_LIB_SOURCES=				\
	dbus-address.c				\
	dbus-auth.c				\
	dbus-auth.h				\
	dbus-auth-script.c			\
	dbus-auth-script.h			\
	dbus-bus.c				\
	dbus-connection.c			\
	dbus-connection-internal.h		\
	dbus-credentials.c			\
	dbus-credentials.h			\
	dbus-errors.c				\
	dbus-keyring.c				\
	dbus-keyring.h				\
	dbus-marshal-header.c			\
	dbus-marshal-header.h			\
	dbus-marshal-byteswap.c			\
	dbus-marshal-byteswap.h			\
	dbus-marshal-recursive.c		\
	dbus-marshal-recursive.h		\
	dbus-marshal-validate.c			\
	dbus-marshal-validate.h			\
	dbus-message.c				\
	dbus-message-internal.h			\
	dbus-message-private.h			\
	dbus-misc.c				\
	dbus-nonce.h				\
	dbus-nonce.c				\
	dbus-object-tree.c			\
	dbus-object-tree.h			\
	dbus-pending-call.c			\
	dbus-pending-call-internal.h		\
	dbus-resources.c			\
	dbus-resources.h			\
	dbus-server.c				\
	dbus-server-debug-pipe.c		\
	dbus-server-debug-pipe.h		\
	dbus-server-protected.h			\
	dbus-server-socket.c			\
	dbus-server-socket.h			\
	$(DBUS_LIB_arch_sources)		\
	dbus-sha.c				\
	dbus-sha.h				\
	dbus-signature.c			\
	dbus-timeout.c				\
	dbus-timeout.h				\
	dbus-threads-internal.h			\
	dbus-threads.c				\
	dbus-transport.c			\
	dbus-transport.h			\
	dbus-transport-protected.h		\
	dbus-transport-socket.c			\
	dbus-transport-socket.h			\
	dbus-watch.c				\
	dbus-watch.h

##	dbus-md5.c				\
##	dbus-md5.h				\

### source code that goes in the installed client library
### AND is generic utility functionality used by the 
### daemon or test programs (all symbols in here should 
### be underscore-prefixed)
DBUS_SHARED_SOURCES=				\
	dbus-dataslot.c				\
	dbus-dataslot.h				\
	dbus-file.c                 \
	dbus-file.h                 \
	dbus-hash.c				\
	dbus-hash.h				\
	dbus-internals.c			\
	dbus-internals.h			\
	dbus-list.c				\
	dbus-list.h				\
	dbus-marshal-basic.c			\
	dbus-marshal-basic.h			\
	dbus-memory.c				\
	dbus-mempool.c				\
	dbus-mempool.h				\
	dbus-pipe.c                 \
	dbus-pipe.h                 \
	dbus-string.c				\
	dbus-string.h				\
	dbus-string-private.h			\
	$(DBUS_SHARED_arch_sources)		\
	dbus-sysdeps.c				\
	dbus-sysdeps.h

### source code that is generic utility functionality used
### by the bus daemon or test apps, but is NOT included
### in the D-Bus client library (all symbols in here 
### should be underscore-prefixed but don't really need 
### to be unless they move to DBUS_SHARED_SOURCES later)
DBUS_UTIL_SOURCES=				\
	dbus-auth-util.c			\
	dbus-credentials-util.c			\
	dbus-mainloop.c				\
	dbus-mainloop.h				\
	dbus-marshal-byteswap-util.c		\
	dbus-marshal-recursive-util.c		\
	dbus-marshal-validate-util.c		\
	dbus-message-factory.c			\
	dbus-message-factory.h			\
	dbus-message-util.c			\
	dbus-shell.c				\
	dbus-shell.h				\
	$(DBUS_UTIL_arch_sources)		\
	dbus-spawn.h				\
	dbus-string-util.c			\
	dbus-sysdeps-util.c			\
	dbus-test.c				\
	dbus-test.h

libdbus_1_la_SOURCES=				\
	$(DBUS_LIB_SOURCES)			\
	$(DBUS_SHARED_SOURCES)

libdbus_convenience_la_SOURCES=			\
	$(DBUS_LIB_SOURCES)			\
	$(DBUS_SHARED_SOURCES)			\
	$(DBUS_UTIL_SOURCES)

symbol_defs = dbus-1.def.pre dbus-1-symbols.def.in dbus-1-testsymbols.def.in dbus-1-verbosesymbols.def.in \
	dbus-convenience.def.pre dbus-internal-symbols.def.in dbus-internal-testsymbols.def.in

BUILT_SOURCES=$(dbusarchinclude_HEADERS) $(built_sources_sys)
EXTRA_DIST=dbus-arch-deps.h.in $(symbol_defs)


## this library is the same as libdbus, but exports all the symbols
## and is only used for static linking within the dbus package.
noinst_LTLIBRARIES=libdbus-convenience.la

libdbus_1_la_CPPFLAGS= -Ddbus_1_EXPORTS
libdbus_1_la_LIBADD= $(DBUS_CLIENT_LIBS)
libdbus_1_la_LDFLAGS= $(export_symbols) -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) -no-undefined @R_DYNAMIC_LDFLAG@ @PIC_LDFLAGS@

libdbus_convenience_la_LIBADD=$(DBUS_CLIENT_LIBS)
libdbus_convenience_la_LDFLAGS=$(export_symbols_convenience) @R_DYNAMIC_LDFLAG@

## note that TESTS has special meaning (stuff to use in make check)
## so if adding tests not to be run in make check, don't add them to 
## TESTS
if DBUS_BUILD_TESTS
TESTS_ENVIRONMENT=DBUS_TEST_DATA=$(top_builddir)/test/data DBUS_TEST_HOMEDIR=$(top_builddir)/dbus
TESTS=dbus-test
else
TESTS=
endif

## we use noinst_PROGRAMS not check_PROGRAMS so that we build 
## even when not doing "make check"
noinst_PROGRAMS=$(TESTS)

dbus_test_SOURCES=				\
	dbus-test-main.c

dbus_test_LDADD=libdbus-convenience.la $(DBUS_TEST_LIBS)
dbus_test_LDFLAGS=@R_DYNAMIC_LDFLAG@

install-data-local: install-def-file

uninstall-local: uninstall-def-file

## mop up the gcov files
clean-local:
	/bin/rm *.bb *.bbg *.da *.gcov .libs/*.da .libs/*.bbg || true

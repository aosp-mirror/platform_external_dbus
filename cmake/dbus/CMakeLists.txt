project(dbus)

#INCLUDES=-I$(top_srcdir) $(DBUS_CLIENT_CFLAGS) -DDBUS_COMPILATION

include_directories(${CMAKE_SOURCE_DIR}/..)
SET(DBUS_DIR ${CMAKE_SOURCE_DIR}/../dbus)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dbus-arch-deps.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/dbus-arch-deps.h )

add_definitions(-DDBUS_COMPILATION)

set (dbusinclude_HEADERS
	${DBUS_DIR}/dbus.h
	${DBUS_DIR}/dbus-address.h
	${DBUS_DIR}/dbus-bus.h
	${DBUS_DIR}/dbus-connection.h
	${DBUS_DIR}/dbus-errors.h
	${DBUS_DIR}/dbus-macros.h
	${DBUS_DIR}/dbus-memory.h
	${DBUS_DIR}/dbus-message.h
	${DBUS_DIR}/dbus-misc.h
	${DBUS_DIR}/dbus-pending-call.h
	${DBUS_DIR}/dbus-protocol.h
	${DBUS_DIR}/dbus-server.h
	${DBUS_DIR}/dbus-shared.h
	${DBUS_DIR}/dbus-signature.h
	${DBUS_DIR}/dbus-threads.h
	${DBUS_DIR}/dbus-types.h
	dbus-arch-deps.h
)

### source code that goes in the installed client library
### and is specific to library functionality
set (DBUS_LIB_SOURCES
	${DBUS_DIR}/dbus-address.c
	${DBUS_DIR}/dbus-auth.c
	${DBUS_DIR}/dbus-auth-script.c
	${DBUS_DIR}/dbus-bus.c
	${DBUS_DIR}/dbus-connection.c
	${DBUS_DIR}/dbus-errors.c
	${DBUS_DIR}/dbus-keyring.c
	${DBUS_DIR}/dbus-marshal-header.c
	${DBUS_DIR}/dbus-marshal-byteswap.c
	${DBUS_DIR}/dbus-marshal-recursive.c
	${DBUS_DIR}/dbus-marshal-validate.c
	${DBUS_DIR}/dbus-message.c
	${DBUS_DIR}/dbus-misc.c
	${DBUS_DIR}/dbus-object-tree.c
	${DBUS_DIR}/dbus-pending-call.c
	${DBUS_DIR}/dbus-resources.c
	${DBUS_DIR}/dbus-server.c
	${DBUS_DIR}/dbus-server-socket.c
	${DBUS_DIR}/dbus-server-debug-pipe.c
	${DBUS_DIR}/dbus-sha.c
	${DBUS_DIR}/dbus-signature.c
	${DBUS_DIR}/dbus-timeout.c
	${DBUS_DIR}/dbus-threads.c
	${DBUS_DIR}/dbus-transport.c
	${DBUS_DIR}/dbus-transport-socket.c
	${DBUS_DIR}/dbus-watch.c
#dbus-md5.c
#
#  find a clean Windows implementation
#  and move code to a *-win.c file
)
if(UNIX)
    set (DBUS_LIB_SOURCES ${DBUS_LIB_SOURCES} 
	    ${DBUS_DIR}/dbus-transport-unix.c
	    ${DBUS_DIR}/dbus-server-unix.c
	)
else(UNIX)
    set (DBUS_LIB_SOURCES ${DBUS_LIB_SOURCES} 
#	    ${DBUS_DIR}/dbus-transport-win.c
	    ${DBUS_DIR}/dbus-server-win.c
	)
endif(UNIX)

set (DBUS_LIB_HEADERS
	${DBUS_DIR}/dbus-auth.h
	${DBUS_DIR}/dbus-auth-script.h
	${DBUS_DIR}/dbus-connection-internal.h
	${DBUS_DIR}/dbus-keyring.h
	${DBUS_DIR}/dbus-marshal-header.h
	${DBUS_DIR}/dbus-marshal-byteswap.h
	${DBUS_DIR}/dbus-marshal-recursive.h
	${DBUS_DIR}/dbus-marshal-validate.h
	${DBUS_DIR}/dbus-message-internal.h
	${DBUS_DIR}/dbus-message-private.h
	${DBUS_DIR}/dbus-misc.h
	${DBUS_DIR}/dbus-object-tree.h
	${DBUS_DIR}/dbus-protocol.h
	${DBUS_DIR}/dbus-resources.h
	${DBUS_DIR}/dbus-server-debug-pipe.h
	${DBUS_DIR}/dbus-server-protected.h
	${DBUS_DIR}/dbus-server-unix.h
	${DBUS_DIR}/dbus-sha.h
	${DBUS_DIR}/dbus-timeout.h
	${DBUS_DIR}/dbus-threads.h
	${DBUS_DIR}/dbus-threads-internal.h
	${DBUS_DIR}/dbus-transport.h
	${DBUS_DIR}/dbus-transport-protected.h
	${DBUS_DIR}/dbus-transport-unix.h
	${DBUS_DIR}/dbus-watch.h
	${CMAKE_BINARY_DIR}/config.h
##dbus-md5.h
)
### source code that goes in the installed client library
### AND is generic utility functionality used by the 
### daemon or test programs (all symbols in here should 
### be underscore-prefixed)
set (DBUS_SHARED_SOURCES
	${DBUS_DIR}/dbus-dataslot.c
	${DBUS_DIR}/dbus-hash.c
	${DBUS_DIR}/dbus-internals.c
	${DBUS_DIR}/dbus-list.c
	${DBUS_DIR}/dbus-marshal-basic.c
	${DBUS_DIR}/dbus-memory.c
	${DBUS_DIR}/dbus-mempool.c
	${DBUS_DIR}/dbus-string.c
	${DBUS_DIR}/dbus-sysdeps.c
	${DBUS_DIR}/dbus-sysdeps-util.c
	${DBUS_DIR}/dbus-userdb.c
)

set (DBUS_SHARED_HEADERS
	${DBUS_DIR}/dbus-dataslot.h
	${DBUS_DIR}/dbus-hash.h
	${DBUS_DIR}/dbus-internals.h
	${DBUS_DIR}/dbus-list.h
	${DBUS_DIR}/dbus-marshal-basic.h
	${DBUS_DIR}/dbus-mempool.h
	${DBUS_DIR}/dbus-string.h
	${DBUS_DIR}/dbus-string-private.h
	${DBUS_DIR}/dbus-sysdeps.h
	${DBUS_DIR}/dbus-userdb.h
)

### source code that is generic utility functionality used
### by the bus daemon or test apps, but is NOT included
### in the D-BUS client library (all symbols in here 
### should be underscore-prefixed but don't really need 
### to be unless they move to DBUS_SHARED_SOURCES later)
set (DBUS_UTIL_SOURCES
	${DBUS_DIR}/dbus-auth-util.c
	${DBUS_DIR}/dbus-mainloop.c
	${DBUS_DIR}/dbus-marshal-byteswap-util.c
	${DBUS_DIR}/dbus-marshal-recursive-util.c
	${DBUS_DIR}/dbus-marshal-validate-util.c
	${DBUS_DIR}/dbus-message-factory.c
	${DBUS_DIR}/dbus-message-util.c
	${DBUS_DIR}/dbus-shell.c
	${DBUS_DIR}/dbus-string-util.c
	${DBUS_DIR}/dbus-userdb-util.c
)

if (DBUS_BUILD_TESTS)
	set (DBUS_UTIL_SOURCES 
		${DBUS_UTIL_SOURCES}
		${DBUS_DIR}/dbus-test.c
	)
endif (DBUS_BUILD_TESTS)

set (DBUS_UTIL_HEADERS
	${DBUS_DIR}/dbus-mainloop.h
	${DBUS_DIR}/dbus-message-factory.h
	${DBUS_DIR}/dbus-shell.h
	${DBUS_DIR}/dbus-spawn.h
	${DBUS_DIR}/dbus-test.h
)

### platform specific settings
if (WIN32)
	set (DBUS_SHARED_SOURCES ${DBUS_SHARED_SOURCES} 
		${DBUS_DIR}/dbus-sysdeps-win.c
		${DBUS_DIR}/dbus-sysdeps-thread-win.c
		${DBUS_DIR}/dbus-sysdeps-util-win.c
		${DBUS_DIR}/dbus-spawn-win.c
	)
	set (DBUS_SHARED_HEADERS ${DBUS_SHARED_HEADERS} 
    ${DBUS_DIR}/dbus-sockets-win.h
    ${DBUS_DIR}/dbus-sysdeps-win.h
  )
	set (LIBS ${LIBS} ws2_32 advapi32 netapi32)

else (WIN32)
	set (DBUS_SHARED_SOURCES ${DBUS_SHARED_SOURCES} 
		${DBUS_DIR}/dbus-sysdeps-unix.c
		${DBUS_DIR}/dbus-sysdeps-pthread.c
		${DBUS_DIR}/dbus-sysdeps-util-unix.c
		${DBUS_DIR}/dbus-spawn.c
	)
	set (DBUS_SHARED_HEADERS ${DBUS_SHARED_HEADERS} 
    ${DBUS_DIR}/dbus-server-unix.h
    ${DBUS_DIR}/dbus-transport-unix.h
    ${DBUS_DIR}/dbus-sysdeps-unix.h
  )
endif (WIN32)

set(libdbus_SOURCES
	${DBUS_LIB_SOURCES}
	${DBUS_SHARED_SOURCES}
	# for debugging
	${DBUS_UTIL_SOURCES}
)

set(libdbus_HEADERS 
	${DBUS_LIB_HEADERS}
	${DBUS_SHARED_HEADERS}
	# for debugging
	${DBUS_UTIL_HEADERS}
)

if(MSVC)
	add_library(dbus-1 STATIC ${libdbus_SOURCES} ${libdbus_HEADERS} )
	project_source_group(${GROUP_CODE} libdbus_SOURCES libdbus_HEADERS)
else(MSVC)
	add_library(dbus-1 ${libdbus_SOURCES} ${libdbus_HEADERS} )
endif(MSVC)

target_link_libraries(dbus-1 ${LIBS} )

install_targets(/lib dbus-1 )
install_files(/include/dbus FILES ${dbusinclude_HEADERS})

#ADD_EXECUTABLE(dbus-example dbus-example.c)
#target_link_libraries(${CMAKE_SOURCE_DIR}/../dbus/dbus-example dbus-1)
#install_targets(/bin dbus-example )

## note that TESTS has special meaning (stuff to use in make check)
## so if adding tests not to be run in make check, don't add them to 
## TESTS
#if DBUS_BUILD_TESTS
#TESTS_ENVIRONMENT=DBUS_TEST_DATA=$(top_builddir)/test/data DBUS_TEST_HOMEDIR=$(top_builddir)/dbus
#TESTS=dbus-test
#else
#TESTS=
#endif

if (DBUS_BUILD_TESTS)
	set (TESTS_ENVIRONMENT "DBUS_TEST_DATA=${CMAKE_SOURCE_DIR}/test/data DBUS_TEST_HOMEDIR=${CMAKE_BUILD_DIR}/dbus")
	ADD_EXECUTABLE(dbus-test ${CMAKE_SOURCE_DIR}/../dbus/dbus-test-main.c)
	target_link_libraries(dbus-test dbus-1 ${LIBS})
	add_test(dbus-test ${EXECUTABLE_OUTPUT_PATH}/dbus-test ${CMAKE_SOURCE_DIR}/../test/data)
	#install_targets(/bin dbus-test)
ENDIF (DBUS_BUILD_TESTS)

if (UNIX)
# set version info
ENDIF (UNIX)


## mop up the gcov files
#clean-local:
#/bin/rm *.bb *.bbg *.da *.gcov .libs/*.da .libs/*.bbg || true


#if HAVE_GLIB
#    GLIB_SUBDIR=glib
#endif
#if HAVE_PYTHON
#    PYTHON_SUBDIR=python
#endif
#if HAVE_QTESTLIB
#    QT_SUBDIR=qt
#endif

add_subdirectory( name-test )

#SUBDIRS=name-test $(GLIB_SUBDIR) $(PYTHON_SUBDIR) $(QT_SUBDIR)
#DIST_SUBDIRS=glib python qt name-test

#INCLUDES=-I$(top_srcdir) $(DBUS_test-CFLAGS) 

include_directories( ${CMAKE_SOURCE_DIR}/.. )
include_directories( ${CMAKE_INCLUDE_PATH} )

if (WIN32)
add_definitions(-Dsleep=_sleep)
endif (WIN32)

set (TEST_SOCKET_DIR "/")

#if (DBUS_BUILD_TESTS)
## break-loader removed for now
#test-BINARIES=test-service test-names test-shell-service shell-test spawn-test test-segfault test-exit test-sleep-forever

#enable stand alone make check test
#TESTS=shell-test
#else
#test-BINARIES=
#TESTS=
#endif

#if DBUS_GCOV_ENABLED
#GCOV_BINARIES=decode-gcov
#else
#GCOV_BINARIES=
#endif

set (test-service_SOURCES
	${CMAKE_SOURCE_DIR}/../test/test-service.c				
	${CMAKE_SOURCE_DIR}/../test/test-utils.c				
	${CMAKE_SOURCE_DIR}/../test/test-utils.h
)

set (test-names_SOURCES
	${CMAKE_SOURCE_DIR}/../test/test-names.c				
	${CMAKE_SOURCE_DIR}/../test/test-utils.c				
	${CMAKE_SOURCE_DIR}/../test/test-utils.h
)

set (break_loader_SOURCES
	${CMAKE_SOURCE_DIR}/../test/break-loader.c
)

set (test-shell-service_SOURCES
	${CMAKE_SOURCE_DIR}/../test/test-shell-service.c			
	${CMAKE_SOURCE_DIR}/../test/test-utils.c				
	${CMAKE_SOURCE_DIR}/../test/test-utils.h
)

set (test-shell_SOURCES
        ${CMAKE_SOURCE_DIR}/../test/shell-test.c
)

set (test-spawn_SOURCES
	${CMAKE_SOURCE_DIR}/../test/spawn-test.c
)

set (test-exit_SOURCES
	${CMAKE_SOURCE_DIR}/../test/test-exit.c
)

set (test-segfault_SOURCES
	${CMAKE_SOURCE_DIR}/../test/test-segfault.c
)

set (test-sleep-forever_SOURCES
	${CMAKE_SOURCE_DIR}/../test/test-sleep-forever.c
)

set (decode_gcov_SOURCES
	${CMAKE_SOURCE_DIR}/../test/decode-gcov.c
)

add_executable(test-service ${test-service_SOURCES})
target_link_libraries(test-service dbus-1)
#install_targets(/bin test-service)

# (rh) does not work at now
ADD_TEST(test-service ${EXECUTABLE_OUTPUT_PATH}/test-service${EXT})



add_executable(test-names ${test-names_SOURCES})
target_link_libraries(test-names dbus-1)
#install_targets(/bin test-names)
ADD_TEST(test-names ${EXECUTABLE_OUTPUT_PATH}/test-names${EXT})

add_executable(test-shell ${test-shell_SOURCES})
target_link_libraries(test-shell dbus-1)
#install_targets(/bin test-shell)
ADD_TEST(test-shell ${EXECUTABLE_OUTPUT_PATH}/test-shell${EXT})

add_executable(test-shell-service ${test-shell_SOURCES})
target_link_libraries(test-shell-service dbus-1)
#install_targets(/bin test-shell-service)
ADD_TEST(test-shell-service ${EXECUTABLE_OUTPUT_PATH}/test-shell-service${EXT})

add_executable(test-spawn ${test-spawn_SOURCES})
target_link_libraries(test-spawn dbus-1)
# (rh) does not work now
#ADD_TEST(test-spawn ${EXECUTABLE_OUTPUT_PATH}/test-spawn${EXT})

add_executable(test-exit ${test-exit_SOURCES})
target_link_libraries(test-exit dbus-1)
#install_targets(/bin test-exit)
ADD_TEST(test-exit ${EXECUTABLE_OUTPUT_PATH}/test-exit${EXT})

add_executable(test-segfault ${test-segfault_SOURCES})
target_link_libraries(test-segfault dbus-1)
#install_targets(/bin test-segfault)
ADD_TEST(test-segfault ${EXECUTABLE_OUTPUT_PATH}/test-segfault${EXT})

add_executable(test-sleep-forever ${test-sleep-forever_SOURCES})
target_link_libraries(test-sleep-forever dbus-1)
# (rh) don't know for what this test is good 
#ADD_TEST(test-sleep-forever ${EXECUTABLE_OUTPUT_PATH}/test-sleep-forever${EXT})

#add_executable(decode-gcov ${decode_gcov_SOURCES})
#target_link_libraries(decode-gcov dbus-1)

#EXTRA_DIST=
#
### keep these in creation order, i.e. uppermost dirs first 
set (TESTDIRS
	test/data					
	test/data/valid-messages			
	test/data/invalid-messages			
	test/data/incomplete-messages		
	test/data/auth				
	test/data/sha-1				
	test/data/valid-config-files			
	test/data/valid-config-files/basic.d		
	test/data/valid-config-files/system.d	
	test/data/valid-service-files		
	test/data/invalid-config-files		
	test/data/equiv-config-files			
	test/data/equiv-config-files/basic		
	test/data/equiv-config-files/basic/basic.d	
	test/data/equiv-config-files/entities	
	test/data/equiv-config-files/entities/basic.d
)
set (CONFIG_VERBOSE 0)

### copy tests to builddir so that generated tests and static tests 
### are all in one place.
FOREACH(FILE_TYPE *.message *.message-raw *.auth-script *.sha1 *.txt *.conf *.service)
	FOREACH(DIR ${TESTDIRS})
		FILE(GLOB FILES "${CMAKE_SOURCE_DIR}/../${DIR}/${FILE_TYPE}" )
		FILE(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/${DIR})
		FOREACH(FILE ${FILES})
			GET_FILENAME_COMPONENT(FILENAME ${FILE} NAME)
			SET (TARGET ${CMAKE_BINARY_DIR}/${DIR}/${FILENAME})
			configure_file(${FILE} ${TARGET} COPYONLY)
			IF (CONFIG_VERBOSE)
				MESSAGE("FROM: ${FILE}\nTO: ${TARGET}\n")
			ENDIF (CONFIG_VERBOSE)
		ENDFOREACH(FILE)
	ENDFOREACH(DIR)
ENDFOREACH(FILE_TYPE)

### generate test files
SET (CONFIG_FILES test/data/valid-config-files)
configure_file(${CMAKE_SOURCE_DIR}/${CONFIG_FILES}/debug-allow-all-sha1.conf.cmake ${CMAKE_BINARY_DIR}/${CONFIG_FILES}/debug-allow-all-sha1.conf )
configure_file(${CMAKE_SOURCE_DIR}/${CONFIG_FILES}/debug-allow-all.conf.cmake ${CMAKE_BINARY_DIR}/${CONFIG_FILES}/debug-allow-all.conf )

# (rh) disabled copying because bus-test could not parse this files for unknown reasons
FILE(GLOB FILES "${CMAKE_BINARY_DIR}/bus/*.conf" )
FOREACH(FILE ${FILES})	
	GET_FILENAME_COMPONENT(FILENAME ${FILE} NAME)
	SET (TARGET ${CMAKE_BINARY_DIR}/${CONFIG_FILES}/${FILENAME})
	configure_file(${FILE} ${TARGET})
	IF (CONFIG_VERBOSE)
		MESSAGE("FROM: ${FILE}\nTO: ${TARGET}\n")
	ENDIF (CONFIG_VERBOSE)
ENDFOREACH(FILE)

# todo: for installation the TEST_..._BINARY variables must reflect the 
#       installation dir or has to be defined relative
#
SET (SERVICE_FILES test/data/valid-service-files)
FILE(GLOB FILES "${CMAKE_SOURCE_DIR}/../${SERVICE_FILES}/*.service.in" )
FOREACH(FILE ${FILES})	
	GET_FILENAME_COMPONENT(FILENAME ${FILE} NAME_WE)
	SET (TARGET ${CMAKE_BINARY_DIR}/${SERVICE_FILES}/${FILENAME}.service)
	IF (CONFIG_VERBOSE)
		MESSAGE("FROM: ${FILE}\nTO: ${TARGET}\n")
	ENDIF (CONFIG_VERBOSE)
	configure_file(${FILE} ${TARGET} )
	#install_files(/data/dbus-1/services FILES ${TARGET})
ENDFOREACH(FILE)

#
#FIND_TESTS=find -name "*.message" -o -name "*.message-raw" -o -name "*.auth-script" -o -name "*.sha1" -o -name "*.txt" -o -name "*.conf" -o -name "*.service"
#
### copy tests to builddir so that generated tests and static tests 
### are all in one place.
#all-local:
#	for D in $(TESTDIRS); do								
#		test -d $(top_builddir)/test/$$D || mkdir $(top_builddir)/test/$$D || exit 1 ;	
#	done ;											
#	if ! (test $(srcdir) = . || test $(srcdir) -ef .) ; then								
#		FILES=`(cd $(srcdir) && $(FIND_TESTS))` ;					
#	        for F in $$FILES; do								
#			SRC=$(srcdir)/$$F ;							
#			DEST=$(top_builddir)/test/$$F ;						
#	                echo '-- Copying test file '$$F ;					
#			cp $$SRC $$DEST || exit 1 ;						
#			chmod u+w $$DEST || exit 1 ;						
#		done ;										
#	else											
#		echo '-- No need to copy test data as srcdir = builddir' ;			
#	fi ;											
#	echo '-- Copying' $(top_builddir)/bus/*.conf 'to test directory' ;			
#	cp $(top_builddir)/bus/*.conf $(top_builddir)/test/data/valid-config-files || exit 1 ;  
#	chmod u+w $(top_builddir)/test/data/valid-config-files/*.conf || exit 1
#
### this doesn't clean generated test data files when srcdir=builddir
#clean-local:
#	if test $(srcdir) != . ; then					
#		FILES=`(cd $(top_builddir)/test && $(FIND_TESTS))` ;	
#	        for F in $$FILES; do					
#			DEST=$(top_builddir)/test/$$F ;			
#	                echo '-- Deleting test file '$$F ;		
#			rm $$DEST || exit 1 ;				
#		done ;							
#		REVERSEDIRS= ;						
#		for D in $(TESTDIRS); do				
#			REVERSEDIRS="$$D $$REVERSEDIRS" ;		
#		done ;							
#		for D in $$REVERSEDIRS; do				
#			rmdir $(top_builddir)/test/$$D || exit 1 ;	
#		done ;							
#	fi

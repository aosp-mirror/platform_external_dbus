project(test)

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

set (shell-test_SOURCES
        ${CMAKE_SOURCE_DIR}/../test/shell-test.c
)

set (spawn-test_SOURCES
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
target_link_libraries(test-service ${DBUS_1})

add_executable(test-names ${test-names_SOURCES})
target_link_libraries(test-names ${DBUS_1})

add_executable(shell-test ${shell-test_SOURCES})
target_link_libraries(shell-test ${DBUS_1})
ADD_TEST(shell-test ${EXECUTABLE_OUTPUT_PATH}/shell-test${EXT})

add_executable(test-shell-service ${test-shell-service_SOURCES})
target_link_libraries(test-shell-service ${DBUS_1})

add_executable(spawn-test ${spawn-test_SOURCES})
target_link_libraries(spawn-test ${DBUS_1})

add_executable(test-exit ${test-exit_SOURCES})
target_link_libraries(test-exit ${DBUS_1})

add_executable(test-segfault ${test-segfault_SOURCES})
target_link_libraries(test-segfault ${DBUS_1})

add_executable(test-sleep-forever ${test-sleep-forever_SOURCES})
target_link_libraries(test-sleep-forever ${DBUS_1})

#add_executable(decode-gcov ${decode_gcov_SOURCES})
#target_link_libraries(decode-gcov ${DBUS_1})

#EXTRA_DIST=
#
### keep these in creation order, i.e. uppermost dirs first 
set (TESTDIRS
	test/data					
	test/data/auth
	test/data/equiv-config-files
	test/data/equiv-config-files/basic
	test/data/equiv-config-files/basic/basic.d
	test/data/equiv-config-files/entities
	test/data/equiv-config-files/entities/basic.d
	test/data/incomplete-messages
	test/data/invalid-config-files
	test/data/invalid-config-files-system
	test/data/invalid-messages
	test/data/invalid-service-files-system
	test/data/sha-1
	test/data/valid-config-files
	test/data/valid-config-files/basic.d		
	test/data/valid-config-files/system.d
	test/data/valid-config-files-system
	test/data/valid-introspection-files
	test/data/valid-messages
	test/data/valid-service-files
	test/data/valid-service-files-system
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

FOREACH(FILE_TYPE *.conf.in *.service.in)
  FOREACH(DIR ${TESTDIRS})
    FILE(GLOB FILES "${CMAKE_SOURCE_DIR}/../${DIR}/${FILE_TYPE}" )
    FILE(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/${DIR})
    FOREACH(FILE ${FILES})
      GET_FILENAME_COMPONENT(FILENAME ${FILE} NAME)
      STRING(REGEX REPLACE "\\.in$" "" FILENAME ${FILENAME})
      SET (TARGET ${CMAKE_BINARY_DIR}/${DIR}/${FILENAME})
      configure_file(${FILE} ${TARGET} @ONLY IMMEDIATE)
      IF (CONFIG_VERBOSE)
        MESSAGE("FROM: ${FILE}\nTO: ${TARGET}\n")
      ENDIF (CONFIG_VERBOSE)
    ENDFOREACH(FILE)
  ENDFOREACH(DIR)
ENDFOREACH(FILE_TYPE)

#
# the following config files are required on windows because the related 
# *.in files are not customized sufficiently. 
# TODO merge test/data/valid-config-files/debug-allow-all*.conf.in and ....cmake
#
SET (CONFIG_FILES test/data/valid-config-files)
set (TEST_LISTEN "tcp:host=localhost,port=1234")
configure_file(${CMAKE_SOURCE_DIR}/../${CONFIG_FILES}/debug-allow-all-sha1.conf.cmake ${CMAKE_BINARY_DIR}/${CONFIG_FILES}/debug-allow-all-sha1.conf )
configure_file(${CMAKE_SOURCE_DIR}/../${CONFIG_FILES}/debug-allow-all.conf.cmake ${CMAKE_BINARY_DIR}/${CONFIG_FILES}/debug-allow-all.conf )  

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

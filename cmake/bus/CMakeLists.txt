
include_directories(${CMAKE_SOURCE_DIR}/..)
include_directories( ${CMAKE_INCLUDE_PATH} )

add_definitions(-DDBUS_COMPILATION)

SET(EFENCE "")
SET(BUS_DIR ${CMAKE_SOURCE_DIR}/../bus)

set (config_DATA
	session.conf				
	system.conf
)

# config files for installation 
FOREACH(file ${config_DATA})
	CONFIGURE_FILE( "${CMAKE_CURRENT_SOURCE_DIR}/${file}.cmake" "${CMAKE_CURRENT_BINARY_DIR}/${file}" IMMEDIATE @ONLY)
ENDFOREACH(file)

# copy services for local daemon start to local service dir data/dbus-1/services
SET (SERVICE_FILES test/data/valid-service-files)
FILE(GLOB FILES "${CMAKE_SOURCE_DIR}/../${SERVICE_FILES}/*.service.in" )
FOREACH(FILE ${FILES})	
	GET_FILENAME_COMPONENT(FILENAME ${FILE} NAME_WE)
	SET (TARGET ${CMAKE_BINARY_DIR}/data/dbus-1/services/${FILENAME}.service)
	IF (CONFIG_VERBOSE)
		MESSAGE("FROM: ${FILE}\nTO: ${TARGET}\n")
	ENDIF (CONFIG_VERBOSE)
	configure_file(${FILE} ${TARGET} )
ENDFOREACH(FILE)

if(DBUS_USE_EXPAT)
    SET (XML_SOURCES ${BUS_DIR}/config-loader-expat.c)
else(DBUS_USE_EXPAT)
    SET (XML_SOURCES ${BUS_DIR}/config-loader-libxml.c)
endif (DBUS_USE_EXPAT)

# after next cvs update
#set (DIR_WATCH_SOURCE ${BUS_DIR}/dir-watch-default.c)
set (DIR_WATCH_SOURCE )

set (BUS_SOURCES 
	${BUS_DIR}/activation.c				
	${BUS_DIR}/activation.h				
	${BUS_DIR}/bus.c					
	${BUS_DIR}/bus.h					
	${BUS_DIR}/config-parser.c				
	${BUS_DIR}/config-parser.h				
	${BUS_DIR}/connection.c				
	${BUS_DIR}/connection.h				
	${BUS_DIR}/desktop-file.c				
	${BUS_DIR}/desktop-file.h				
	${BUS_DIR}/dir-watch-default.c				
	${BUS_DIR}/dir-watch.h				
	${BUS_DIR}/dispatch.c				
	${BUS_DIR}/dispatch.h				
	${BUS_DIR}/driver.c				
	${BUS_DIR}/driver.h				
	${BUS_DIR}/expirelist.c				
	${BUS_DIR}/expirelist.h				
	${BUS_DIR}/policy.c				
	${BUS_DIR}/policy.h				
	${BUS_DIR}/selinux.h				
	${BUS_DIR}/selinux.c				
	${BUS_DIR}/services.c				
	${BUS_DIR}/services.h				
	${BUS_DIR}/signals.c				
	${BUS_DIR}/signals.h				
	${BUS_DIR}/test.c					
	${BUS_DIR}/test.h					
	${BUS_DIR}/utils.c					
	${BUS_DIR}/utils.h					
	${XML_SOURCES}
	${DIR_WATCH_SOURCE}
)

set (dbus_daemon_SOURCES
	${BUS_SOURCES}
	${BUS_DIR}/main.c					
)

include_directories(${XML_INCLUDE_DIR})
SET (LIBS ${XML_LIBRARY})

add_executable(dbus-daemon${CMAKE_EXE_POSTFIX} ${dbus_daemon_SOURCES})
target_link_libraries(dbus-daemon${CMAKE_EXE_POSTFIX} dbus-1 ${LIBS})
install_targets(/bin dbus-daemon${CMAKE_EXE_POSTFIX} )
install_files(/etc FILES ${config_DATA})

if (DBUS_SERVICE)
	set (dbus_service_SOURCES 
		${BUS_DIR}/bus-service-win.c
  # TODO: add additional files
	#	${BUS_DIR}/service-main.c
	#	${BUS_SOURCES} 
	)

	add_executable(dbus-service${CMAKE_EXE_POSTFIX} ${dbus_service_SOURCES} )
	target_link_libraries(dbus-service${CMAKE_EXE_POSTFIX} dbus-1 ${LIBS})
	install_targets(/bin dbus-service${CMAKE_EXE_POSTFIX} )

endif (DBUS_SERVICE)

## note that TESTS has special meaning (stuff to use in make check)
## so if adding tests not to be run in make check, don't add them to 
## TESTS
#if DBUS_BUILD_TESTS
#TESTS_ENVIRONMENT=DBUS_TEST_DATA=$(top_builddir)/test/data DBUS_TEST_HOMEDIR=$(top_builddir)/dbus
#TESTS=bus-test 
#else
#TESTS=
#endif

## we use noinst_PROGRAMS not check_PROGRAMS so that we build 
## even when not doing "make check"
#noinst_PROGRAMS=$(TESTS) 

set (bus_test_SOURCES
	${BUS_SOURCES}
	${BUS_DIR}/test-main.c
)

if (DBUS_BUILD_TESTS)	
	add_executable(bus-test${CMAKE_EXE_POSTFIX} ${bus_test_SOURCES})
	target_link_libraries(bus-test${CMAKE_EXE_POSTFIX} dbus-1 ${LIBS} )
	install_targets(/bin bus-test${CMAKE_EXE_POSTFIX})
	add_test(bus-test ${EXECUTABLE_OUTPUT_PATH}/bus-test${CMAKE_EXE_POSTFIX} ${CMAKE_SOURCE_DIR}/../test/data)
endif (DBUS_BUILD_TESTS)

if(MSVC)
	project_source_group(${GROUP_CODE} bus_test_SOURCES dummy)
endif(MSVC)

## mop up the gcov files
#clean-local:
#	/bin/rm *.bb *.bbg *.da *.gcov || true

#install-data-hook:
#	$(mkinstalldirs) $(DESTDIR)/$(localstatedir)/run/dbus
#	$(mkinstalldirs) $(DESTDIR)/$(configdir)/system.d
#	$(mkinstalldirs) $(DESTDIR)/$(datadir)/dbus-1/services

##install_file(${configdir}/system.d FILE



#### Init scripts fun
#SCRIPT_IN_FILES=messagebus.in
#		rc.messagebus.in

## Red Hat start
#if DBUS_INIT_SCRIPTS_RED_HAT

#initddir=$(sysconfdir)/rc.d/init.d

#initd_SCRIPTS= 	
#	messagebus

#endif
# ## Red Hat end

## Slackware start
#if DBUS_INIT_SCRIPTS_SLACKWARE

#initddir=$(sysconfdir)/rc.d/

#initd_SCRIPTS= 	
#	rc.messagebus

#endif
## Slackware end

#MAN_IN_FILES=dbus-daemon.1.in
#man_MANS = dbus-daemon.1

#### Extra dist 

#EXTRA_DIST=$(CONFIG_IN_FILES) $(SCRIPT_IN_FILES) $(man_MANS) $(MAN_IN_FILES)

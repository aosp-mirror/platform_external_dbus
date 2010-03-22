if (DBUS_BUILD_TESTS)

set (NAMEtest-DIR ../../../test/name-test)

add_definitions(-DDBUS_COMPILATION)

add_executable(test-pending-call-dispatch ${NAMEtest-DIR}/test-pending-call-dispatch.c)
target_link_libraries(test-pending-call-dispatch ${DBUS_INTERNAL_LIBRARIES})
ADD_TEST(test-pending-call-dispatch ${EXECUTABLE_OUTPUT_PATH}/test-pending-call-dispatch)

add_executable(test-thread-init ${NAMEtest-DIR}/test-threads-init.c)
target_link_libraries(test-thread-init ${DBUS_INTERNAL_LIBRARIES})
ADD_TEST(test-thread-init ${EXECUTABLE_OUTPUT_PATH}/test-thread-init)

endif (DBUS_BUILD_TESTS)

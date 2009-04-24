if (DBUS_BUILD_TESTS)

set (NAMEtest-DIR ../../../test/name-test)

add_definitions(-DDBUS_COMPILATION)

# there is also a test-names executable, don't know if this is the same
add_executable(test-names1 ${NAMEtest-DIR}/test-names.c)
target_link_libraries(test-names1 ${DBUS_1})
#install_targets(/bin test-names1)
ADD_TEST(test-names ${EXECUTABLE_OUTPUT_PATH}/test-names1)

add_executable(test-pending-call-dispatch ${NAMEtest-DIR}/test-pending-call-dispatch.c)
target_link_libraries(test-pending-call-dispatch ${DBUS_1})
#install_targets(/bin test-pending-call-dispatch)
ADD_TEST(test-pending-call-dispatch ${EXECUTABLE_OUTPUT_PATH}/test-pending_call-dispatch)

add_executable(test-thread-init ${NAMEtest-DIR}/test-threads-init.c)
target_link_libraries(test-thread-init ${DBUS_1})
#install_targets(/bin test-thread-init)
ADD_TEST(test-names ${EXECUTABLE_OUTPUT_PATH}/test-thread-init)

endif (DBUS_BUILD_TESTS)

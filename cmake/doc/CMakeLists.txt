find_package(Doxygen)

if(DOXYGEN_EXECUTABLE)
  OPTION(DBUS_ENABLE_DOXYGEN_DOCS "build DOXYGEN documentation (requires Doxygen)" ON)
endif(DOXYGEN_EXECUTABLE)


set (top_srcdir ${CMAKE_SOURCE_DIR}/..)
configure_file(${CMAKE_SOURCE_DIR}/Doxyfile.cmake ${CMAKE_BINARY_DIR}/Doxyfile )
if (DBUS_ENABLE_DOXYGEN_DOCS)
    add_custom_target(doc 
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
    )
endif (DBUS_ENABLE_DOXYGEN_DOCS)

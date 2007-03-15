find_package(Doxygen)

if(DOXYGEN_EXECUTABLE)
  OPTION(DBUS_ENABLE_DOXYGEN_DOCS "build DOXYGEN documentation (requires Doxygen)" ON)
endif(DOXYGEN_EXECUTABLE)

if (DBUS_ENABLE_DOXYGEN_DOCS)
    set (top_srcdir ${CMAKE_SOURCE_DIR}/..)
    configure_file(${CMAKE_SOURCE_DIR}/Doxyfile.cmake ${CMAKE_BINARY_DIR}/Doxyfile )
    add_custom_target(doc 
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
    )
endif (DBUS_ENABLE_DOXYGEN_DOCS)

#AC_ARG_ENABLE(xml-docs, AS_HELP_STRING([--enable-xml-docs],[build XML documentation (requires xmlto)]),enable_xml_docs=$enableval,enable_xml_docs=auto)
#xmldocs missing

# depends on xmlto
set(DBUS_XML_DOCS_ENABLED 0)

set (EXTRA_DIST 	
	busconfig.dtd			
	introspect.dtd			
	dbus-faq.xml			
	dbus-specification.xml	
	dbus-test-plan.xml		
	dbus-tutorial.xml		
	dcop-howto.txt			
	file-boilerplate.c		
	introspect.xsl
)

set (HTML_FILES
	dbus-faq.html			
	dbus-specification.html	
	dbus-test-plan.html		
	dbus-tutorial.html
)

if (DBUS_XML_DOCS_ENABLED)

macro (DOCBOOK _sources _options)
  get_filename_component(_tmp_FILE ${_sources} ABSOLUTE)
  get_filename_component(_basename ${_tmp_FILE} NAME_WE)
  set(_header ${CMAKE_CURRENT_BINARY_DIR}/${_basename}.html)
  MESSAGE (${_header} ${_sources} ${_options} ${_basename})

  ADD_CUSTOM_TARGET(${_basename}.html ALL
    xmlto.bat ${_sources} 
#    xmlto.bat ${_options} ${_sources} 
    DEPENDS ${_sources} 
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
endmacro (DOCBOOK)

### copy tests to builddir so that generated tests and static tests 
### are all in one place.
### todo how to add more filetypes 
MACRO (COPYDIR _src _type)
    FOREACH(FILE_TYPE ${_type})
    	FOREACH(DIR ${_src})
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
ENDMACRO (COPYDIR)

COPYDIR(doc *.png)
COPYDIR(doc *.svg)

DOCBOOK(${CMAKE_SOURCE_DIR}/../doc/dbus-test-plan.xml html-nochunks)
DOCBOOK(${CMAKE_SOURCE_DIR}/../doc/dbus-tutorial.xml html-nochunks)
DOCBOOK(${CMAKE_SOURCE_DIR}/../doc/dbus-specification.xml html-nochunks)
DOCBOOK(${CMAKE_SOURCE_DIR}/../doc/dbus-faq.xml html-nochunks)

endif(DBUS_XML_DOCS_ENABLED)

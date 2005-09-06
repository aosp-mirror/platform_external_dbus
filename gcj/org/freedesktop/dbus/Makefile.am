INCLUDES=						\
	-I$(top_srcdir) 				\
	-I$(top_builddir)/gcj/org/freedesktop/dbus	\
	 $(DBUS_CLIENT_CFLAGS) 


GCJH = gcjh

lib_LTLIBRARIES = libdbus-gcj-1.la

MOSTLYCLEANFILES = $(generated_headers) $(java_class_files)


java_sources = 				\
	Message.java

native_sources = 			\
	natMessage.cc

generated_headers = $(java_sources:.java=.h)
java_class_files = $(java_sources:.java=.class)
native_object_files = $(native_sources:.cc=.lo)
java_object_files = $(java_sources:.java=.lo)

.class.h:
	$(GCJH) $(basename $<)

.java.class:
	$(GCJ) -C $< -d ../../..

.cc.lo:
	$(LIBTOOL) --mode=compile $(CXX) $(INCLUDES) $(CXXFLAGS) $(CPPFLAGS) -c -o '$@' '$<'

.java.lo:
	$(LIBTOOL) --mode=compile $(GCJ) $(INCLUDES) $(GCJ_FLAGS) -c -o '$@' '$<'

libdbus_gcj_1_la_SOURCES =

libdbus_gcj_1_la_DEPENDENCIES = 	\
	$(generated_headers)		\
	$(native_object_files)		\
	$(java_object_files)		\
	$(java_class_files)


libdbus_gcj_1_la_LDFLAGS = -no-undefined

libdbus_gcj_1_la_LIBADD = 		\
	$(native_object_files)		\
	$(java_object_files)

EXTRA_libdbus_gcj_1_la_SOURCES = 	\
	$(native_sources)		\
	$(java_sources)

libdbus_gcj_1_la_LINK = $(LIBTOOL) --tag=GCJ --mode=link $(GCJ) -L$(here) $(JC1FLAGS) $(LDFLAGS) -o $@


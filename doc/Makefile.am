EXTRA_DIST= 					\
	dbus-specification.html			\
	dbus-specification.sgml			\
	dcop-howto.txt				\
	file-boilerplate.c

if MAINTAINER_MODE
all-local: dbus-specification.html
endif

dbus-specification.html: dbus-specification.sgml
	db2html -o . --nochunks dbus-specification.sgml &&	\
	rm -r dbus-specification/stylesheet-images &&		\
	rmdir dbus-specification

maintainer-clean-local:
	rm -f dbus-specification.html
	rm -rf dbus-specification/stylesheet-images
	test -d dbus-specification && rmdir dbus-specification

EXTRA_DIST= 					\
	config-file.txt				\
	dbus-specification.html			\
	dbus-specification.sgml			\
	dbus-test-plan.html			\
	dbus-test-plan.sgml			\
	dcop-howto.txt				\
	file-boilerplate.c

if MAINTAINER_MODE
all-local: dbus-specification.html dbus-test-plan.html
endif

dbus-specification.html: dbus-specification.sgml
	db2html -o . --nochunks dbus-specification.sgml &&	\
	rm -r dbus-specification/stylesheet-images &&		\
	(if test -d dbus-specification ; then rmdir dbus-specification ; fi)

dbus-test-plan.html: dbus-test-plan.sgml
	db2html -o . --nochunks dbus-test-plan.sgml &&		\
	rm -r dbus-test-plan/stylesheet-images &&		\
	(if test -d dbus-test-plan ; then rmdir dbus-test-plan ; fi)

maintainer-clean-local:
	rm -f dbus-test-plan.html
	rm -rf dbus-test-plan/stylesheet-images
	test -d dbus-test-plan && rmdir dbus-test-plan
	rm -f dbus-specification.html
	rm -rf dbus-specification/stylesheet-images
	test -d dbus-specification && rmdir dbus-specification

EXTRA_DIST= 					\
	dbus-specification.html			\
	dbus-specification.sgml			\
	dbus-test-plan.html			\
	dbus-test-plan.sgml			\
	dcop-howto.txt				\
	file-boilerplate.c

if DBUS_DOCS_ENABLED
all-local: dbus-specification.html dbus-test-plan.html
endif

dbus-specification.html: dbus-specification.sgml
	$(DB2HTML) -o . --nochunks $< &&				\
	rm -rf $(srcdir)/dbus-specification/stylesheet-images &&	\
	(if test -d $(srcdir)/dbus-specification ; then			\
	rmdir $(srcdir)/dbus-specification ; fi)

dbus-test-plan.html: dbus-test-plan.sgml
	$(DB2HTML) -o . --nochunks $< &&			\
	rm -rf $(srcdir)/dbus-test-plan/stylesheet-images &&	\
	(if test -d $(srcdir)/dbus-test-plan ; then		\
	rmdir $(srcdir)/dbus-test-plan ; fi)

maintainer-clean-local:
	rm -f dbus-test-plan.html
	rm -rf dbus-test-plan/stylesheet-images
	test -d dbus-test-plan && rmdir dbus-test-plan
	rm -f dbus-specification.html
	rm -rf dbus-specification/stylesheet-images
	test -d dbus-specification && rmdir dbus-specification

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

## Debian db2html outputs to stdout, so we collect stdout and use 
## it if we didn't get the proper output

dbus-specification.html: dbus-specification.sgml
	rm -f $@ &&							\
	$(DB2HTML) -o . --nochunks $< > $@.stdout &&			\
	(test -e $@ && rm -f $@.stdout || mv $@.stdout $@) &&		\
	rm -rf $(srcdir)/dbus-specification/stylesheet-images &&	\
	(if test -d $(srcdir)/dbus-specification ; then			\
	rmdir $(srcdir)/dbus-specification ; fi)

dbus-test-plan.html: dbus-test-plan.sgml
	rm -f $@ &&						\
	$(DB2HTML) -o . --nochunks $< > $@.stdout &&		\
	(test -e $@ && rm -f $@.stdout || mv $@.stdout $@) &&	\
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

EXTRA_DIST= 					\
	dbus-specification.xml			\
	dbus-test-plan.xml			\
	dbus-tutorial.xml			\
	dcop-howto.txt				\
	file-boilerplate.c

HTML_FILES=					\
	dbus-specification.html			\
	dbus-test-plan.html			\
	dbus-tutorial.html

if DBUS_XML_DOCS_ENABLED
all-local: $(HTML_FILES)

EXTRA_DIST += $(HTML_FILES)

dbus-specification.html: dbus-specification.xml
	$(XMLTO) html-nochunks $<

dbus-test-plan.html: dbus-test-plan.xml
	$(XMLTO) html-nochunks $<

dbus-tutorial.html: dbus-tutorial.xml
	$(XMLTO) html-nochunks $<
endif

maintainer-clean-local:
	rm -f $(HTML_FILES)

INCLUDES=-I$(top_srcdir)/qt $(DBUS_CLIENT_CFLAGS) $(DBUS_QT_CFLAGS) -DDBUS_COMPILATION
LDADD = ../src/libdbus-qt4-1.la

if HAVE_QT_GUI
chat_LDADD = $(LDADD) $(DBUS_QT_GUI_LIBS)
dist_chat_SOURCES = chat.cpp chat.h chatadaptor.cpp
nodist_chat_SOURCES = chatinterface.cpp
chat.o: chatmainwindow.h chatsetnickname.h chatinterface.h chatadaptor.h chat.moc chatadaptor.moc
chatmainwindow.h: chatmainwindow.ui
chatsetnickname.h: chatsetnickname.ui
chatinterface.cpp chatinterface.h: com.trolltech.ChatInterface.xml
	../tools/dbusidl2cpp -m -p chatinterface $?
	$(QT_MOC) -o chatinterface.moc chatinterface.h

CHAT=chat
endif


noinst_PROGRAMS = hello listnames dbus ping pong complexping complexpong $(CHAT)
hello_SOURCES = hello.cpp
dbus_SOURCES = dbus.cpp
listnames_SOURCES = listnames.cpp

ping_SOURCES = ping.cpp
pong_SOURCES = pong.cpp pong.h
pong.o: pong.moc

complexping_SOURCES = complexping.cpp complexping.h
complexpong_SOURCES = complexpong.cpp complexpong.h
complexpong.o: complexpong.moc
complexping.o: complexping.moc

EXTRA_DIST = ping-common.h chatmainwindow.ui chatsetnickname.ui com.trolltech.ChatInterface.xml chatadaptor.h

CLEANFILES = chat.moc chatadaptor.moc complexping.moc complexpong.moc pong.moc \
	chatinterface.cpp chatinterface.h chatinterface.moc \
	chatmainwindow.h chatsetnickname.h

%.moc: %.h
	$(QT_MOC) $< > $@
%.h: %.ui
	$(QT_UIC) -o $@ $?


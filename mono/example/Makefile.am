DESTDIR=

NOINST_EXES=echo-server.exe echo-client.exe bus-listener.exe
DISTCLEANFILES=$(NOINST_EXES)

all: $(NOINST_EXES)

echo-server.exe: EchoServer.cs Echoer.cs
	$(CSC) --target exe -L .. -r:../dbus-sharp.dll -pkg:gtk-sharp -o echo-server.exe $(srcdir)/EchoServer.cs $(srcdir)/Echoer.cs

echo-client.exe: EchoClient.cs Echoer.cs
	$(CSC) --target exe -L .. -r:../dbus-sharp.dll -o echo-client.exe $(srcdir)/EchoClient.cs $(srcdir)/Echoer.cs

bus-listener.exe: BusListener.cs
	$(CSC) --target exe -L .. -r:../dbus-sharp.dll -pkg:gtk-sharp -o bus-listener.exe $(srcdir)/BusListener.cs


clean:
	rm -f $(NOINST_EXES)

install: all

EXTRA_DIST=EchoServer.cs EchoClient.cs Echoer.cs BusListener.cs


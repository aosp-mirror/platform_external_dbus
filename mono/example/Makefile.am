DESTDIR=

NOINST_EXES=echo-server.exe echo-client.exe

all: $(NOINST_EXES)

echo-server.exe: EchoServer.cs Echoer.cs
	$(MCS) $(MCSFLAGS) --unsafe --target exe -L .. -r dbus-sharp.dll -r gtk-sharp -o echo-server.exe EchoServer.cs Echoer.cs

echo-client.exe: EchoClient.cs Echoer.cs
	$(MCS) $(MCSFLAGS) --unsafe --target exe -L .. -r dbus-sharp.dll -o echo-client.exe EchoClient.cs Echoer.cs

clean:
	rm -f $(NOINST_EXES)

install: all

EXTRA_DIST=EchoServer.cs EchoClient.cs Echoer.cs


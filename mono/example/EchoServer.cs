namespace Foo
{
  using System;
  using DBus;
  using Gtk;

  public class EchoServer
  {
    public static int Main(string [] args)
    {
      Application.Init();
      
      Connection connection = Bus.GetSessionBus();
      Service service = new Service(connection, "org.freedesktop.Test");
      Echoer echoer = new Echoer();
      service.RegisterObject(echoer, "/org/freedesktop/Test/Echoer");
      
      Application.Run();
      
      return 0;
    }
  }
}

namespace Foo
{
  using System;
  using DBus;

  public class EchoClient
  {
    public static int Main(string [] args)
    {
      Connection connection = Bus.GetSessionBus();
      Service service = Service.Get(connection, "org.freedesktop.Test");
      Echoer echoer = (Echoer) 
	service.GetObject(typeof(Echoer), "/org/freedesktop/Test/Echoer");
      System.Console.WriteLine(echoer.Echo("Hello world!"));
      
      return 0;
    }
  }
}

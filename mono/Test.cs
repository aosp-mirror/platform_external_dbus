using System;
using System.Threading;
using DBus;
using Gtk;

namespace DBus.Test
{
  public class Test
  {
    public static Service service = null;
    public static Connection connection = null;
    
    public static int Main(string [] args)
    {
      TestServer testServer = new TestServer();
      Thread serverThread = new Thread(new ThreadStart(testServer.StartServer));
      serverThread.Start();

      connection = Bus.GetSessionBus();
      service = Service.Get(connection, "org.freedesktop.Test");
      Thread.Sleep (1000);

      TestObject testObject = (TestObject) service.GetObject(typeof(TestObject), "/org/freedesktop/Test/TestObject");

      Console.WriteLine ("Got object [{0}]", testObject);
      
      System.Console.WriteLine(testObject.Test1("Hello"));

      Console.WriteLine ("Got object [{0}]", testObject);

      //RunTests(testObject);

      return 0;
    }

    public static void RunTests(TestObject testObject) 
    {
      System.Console.WriteLine(testObject.Test1("Hello"));
    }
  }

  public class TestServer
  {
    public Connection connection;
    public Service service;

    public TestServer()
    {
      Application.Init();
      
      System.Console.WriteLine("Starting server...");

      connection = Bus.GetSessionBus();
      service = new Service(connection, "org.freedesktop.Test");
      TestObject testObject = new TestObject();
      service.RegisterObject(testObject, "/org/freedesktop/Test/TestObject");
      
      System.Console.WriteLine("Foo!");
    }
    
    public void StartServer()
    {
      Application.Run();
    }
  }

  [Interface("org.freedesktop.Test.TestObject")]
  public class TestObject
  {
    [Method]
    public virtual int Test1(string x)
    {
      System.Console.WriteLine("Called: " + x);
      return 5;
    }
  }    
}

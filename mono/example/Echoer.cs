namespace Foo
{
  using System;
  using DBus;

  [Interface("org.freedesktop.Test.Echoer")]
  public class Echoer
  {
    [Method]
    public virtual string Echo(string message)
    {
      System.Console.WriteLine("I received: " + message);
      return "Reply: " + message;
    }
  }
}

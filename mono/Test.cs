
using System;

class Test {  
  static void Main() {
    DBus.Message m;

    m = new DBus.Message ("org.freedesktop.Foo", null);

    Console.WriteLine ("Message name is {0}\n", m.Name);
  }
}

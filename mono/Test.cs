
using System;

class Test {  
  static void Main() {
    DBus.Message m;
    DBus.Connection c;

    c = new DBus.Connection ("unix:path=/tmp/foobar");

    m = new DBus.Message ("org.freedesktop.Foo", null);

    Console.WriteLine ("Message name is {0}\n", m.Name);
  }
}

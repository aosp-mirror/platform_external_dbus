
using System;

class Test {  
  static void Main() {
    DBus.Message m;
    DBus.Connection c;

    // c = new DBus.Connection ("unix:path=/tmp/foobar");

    c = DBus.Connection.GetBus (DBus.Connection.BusType.Session);
    
    m = new DBus.Message ("org.freedesktop.Foo",
                          "org.freedesktop.DBus.Broadcast");

    c.Send (m);
    c.Flush ();    
  }
}


using System;
using System.Runtime.InteropServices;

class Test {  
  static void Main() {    
    g_thread_init (IntPtr.Zero);
    
    DBus.Connection c;

    // c = new DBus.Connection ("unix:path=/tmp/foobar");

    try { 
      c = DBus.Connection.GetBus (DBus.Connection.BusType.Session);
    }
    catch (DBus.Exception e) {
      Console.Error.WriteLine ("Failed to open connection: {0}",
                               e.Message);
      return;
    }
      
    DBus.Message m = new DBus.Message ("org.freedesktop.Foo",
                                       "org.freedesktop.DBus.Broadcast");

    c.Send (m);
    c.Flush ();

    IntPtr loop = g_main_loop_new (IntPtr.Zero, false);

    g_main_loop_run (loop);

    g_main_loop_unref (loop);
  }

  internal const string GLibname = "libglib-2.0.so.0";
  internal const string GThreadname = "libgthread-2.0.so.0";
  
  [DllImport (GLibname, EntryPoint="g_main_loop_new")]
    private extern static IntPtr g_main_loop_new (IntPtr context,
                                                  bool   is_running);

  [DllImport (GLibname, EntryPoint="g_main_loop_unref")]
    private extern static void g_main_loop_unref (IntPtr loop);

  [DllImport (GLibname, EntryPoint="g_main_loop_run")]
    private extern static void g_main_loop_run (IntPtr loop);

  [DllImport (GLibname, EntryPoint="g_main_loop_quit")]
    private extern static void g_main_loop_quit (IntPtr loop);
  
  [DllImport (GThreadname, EntryPoint="g_thread_init")]
    private extern static void g_thread_init (IntPtr vtable);
}

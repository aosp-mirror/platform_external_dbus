namespace DBus {

  using System;
  using System.Runtime.InteropServices;
  
  public class Exception : ApplicationException {
    internal Exception (ref Error error)
      : base (Runtime.InteropServices.Marshal.PtrToStringAnsi (error.message)) { }
  }
  
  internal class Internals {
    internal const string DBusLibname = "libdbus-1.so.0";
    internal const string DBusGLibname = "libdbus-glib-1.so.0";
    internal const string GLibname = "libglib-2.0.so.0";
    internal const string GThreadname = "libgthread-2.0.so.0";
    
    internal static void Init () {
        dbus_gthread_init ();
    }
    
    [DllImport (DBus.Internals.DBusGLibname, EntryPoint="dbus_gthread_init")]
      private extern static void dbus_gthread_init ();
  }
}

namespace DBus {

  using System;
  using System.Runtime.InteropServices;
  
  // FIXME add code to verify that size of DBus.Error
  // matches the C code.

  [StructLayout (LayoutKind.Sequential)]
  internal struct Error {
    internal IntPtr name;
    internal IntPtr message;
    private int dummies;
    private IntPtr padding1;

    internal void Init () {
      dbus_error_init (ref this);
    }
    
    internal void Free () {
      dbus_error_free (ref this);
    }
    
    [DllImport (DBus.Internals.Libname, EntryPoint="dbus_error_init")]
    private extern static void dbus_error_init (ref Error error);
    [DllImport (DBus.Internals.Libname, EntryPoint="dbus_error_free")]
    private extern static void dbus_error_free (ref Error error);
  }
}

namespace DBus {

  using System;
  
  public class Exception : ApplicationException {
    internal Exception (ref Error error)
      : base (Runtime.InteropServices.Marshal.PtrToStringAnsi (error.message)) { }
  }
  
  public class Internals {
    public const string Libname = "libdbus-1.so.0";
  }
}

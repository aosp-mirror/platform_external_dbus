namespace DBus {
  
  using System;
  using System.Runtime.InteropServices;
  
  public class Message {

    public Message (string name,
                    string dest_service) {
      raw = dbus_message_new (name, dest_service);
    }

    public string Name {
      get {
        return dbus_message_get_name (raw);
      }
    }
    
    IntPtr raw;

    ~Message () {
      dbus_message_unref (raw);
    }
    
    Message (IntPtr r) {
      raw = r;
      dbus_message_ref (r);
    }
    
    // static constructor runs before any methods 
    static Message () {
      
    }
      
    const string libname = "libdbus-1.so.0";
    
    [DllImport (libname, EntryPoint="dbus_message_new")]
      private extern static IntPtr dbus_message_new (string name,
                                                     string dest_service);

    [DllImport (libname, EntryPoint="dbus_message_unref")]
      private extern static void dbus_message_unref (IntPtr ptr);

    [DllImport (libname, EntryPoint="dbus_message_ref")]
      private extern static void dbus_message_ref (IntPtr ptr);

    [DllImport (libname, EntryPoint="dbus_message_get_name")]
      private extern static string dbus_message_get_name (IntPtr ptr);
  }
}

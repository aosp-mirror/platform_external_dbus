namespace DBus {
  
  using System;
  using System.Runtime.InteropServices;
  using System.Diagnostics;
  
  public class Connection {

    public Connection (string address) {
      // the assignment bumps the refcount
      Error error = new Error ();
      error.Init ();
      raw = dbus_connection_open (address, ref error);
      if (raw != (IntPtr) 0) {
        dbus_connection_unref (raw);
      } else {
        Exception e = new Exception (ref error);
        error.Free ();
        throw e;
      }
    }

    // Keep in sync with C
    public enum BusType {
      Session = 0,
      System = 1,
      Activation = 2
    }

    public static Connection GetBus (BusType bus) {
      Error error = new Error ();

      error.Init ();
      
      IntPtr ptr = dbus_bus_get ((int) bus, ref error);
      if (ptr != (IntPtr) 0) {
        Connection c = Wrap (ptr);
        dbus_connection_unref (ptr);
        return c;
      } else {
        Exception e = new Exception (ref error);
        error.Free ();
        throw e;        
      }
    }
    
    public void Send (Message m,
                      ref int serial) {
      if (!dbus_connection_send (raw, m.raw, ref serial))
        throw new OutOfMemoryException ();
    }

    public void Send (Message m) {
      int ignored = 0;
      Send (m, ref ignored);
    }

    public void Flush () {
      dbus_connection_flush (raw);
    }

    public void Disconnect () {
      dbus_connection_disconnect (raw);
    }
    
    public static Connection Wrap (IntPtr ptr) {
      IntPtr gch_ptr;
      
      gch_ptr = dbus_connection_get_data (ptr, wrapper_slot);
      if (gch_ptr != (IntPtr) 0) {
        return (DBus.Connection) ((GCHandle)gch_ptr).Target;
      } else {
        return new Connection (ptr);
      }
    }

    // surely there's a convention for this pattern with the property
    // and the real member
    IntPtr raw_;
    internal IntPtr raw {
      get {
        return raw_; 
      }
      set {
        if (value == raw_)
          return;
        
        if (raw_ != (IntPtr) 0) {
          IntPtr gch_ptr;
          
          gch_ptr = dbus_connection_get_data (raw_,
                                              wrapper_slot);
          Debug.Assert (gch_ptr != (IntPtr) 0);

          dbus_connection_set_data (raw_, wrapper_slot,
                                    (IntPtr) 0, (IntPtr) 0);
          
          ((GCHandle) gch_ptr).Free ();
          
          dbus_connection_unref (raw_);
        }
        
        raw_ = value;

        if (raw_ != (IntPtr) 0) {
          GCHandle gch;

          dbus_connection_ref (raw_);

          // We store a weak reference to the C# object on the C object
          gch = GCHandle.Alloc (this, GCHandleType.WeakTrackResurrection);
          
          dbus_connection_set_data (raw_, wrapper_slot,
                                    (IntPtr) gch, (IntPtr) 0);
        }
      }
    }

    ~Connection () {
      if (raw != (IntPtr) 0) {
        Disconnect ();
      }
      raw = (IntPtr) 0; // free the native object
    }
    
    Connection (IntPtr r) {
      raw = r;
    }
    
    // static constructor runs before any methods 
    static Connection () {
      DBus.Internals.Init ();
      
      Debug.Assert (wrapper_slot == -1);
      
      if (!dbus_connection_allocate_data_slot (ref wrapper_slot))
        throw new OutOfMemoryException ();

      Debug.Assert (wrapper_slot >= 0);
    }

    // slot used to store the C# object on the C object
    static int wrapper_slot = -1;
    
    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_connection_open")]
      private extern static IntPtr dbus_connection_open (string address,
                                                         ref Error error);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_connection_unref")]
      private extern static void dbus_connection_unref (IntPtr ptr);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_connection_ref")]
      private extern static void dbus_connection_ref (IntPtr ptr);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_connection_allocate_data_slot")]
      private extern static bool dbus_connection_allocate_data_slot (ref int slot);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_connection_free_data_slot")]
      private extern static void dbus_connection_free_data_slot (ref int slot);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_connection_set_data")]
      private extern static bool dbus_connection_set_data (IntPtr ptr,
                                                           int    slot,
                                                           IntPtr data,
                                                           IntPtr free_data_func);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_connection_send")]
      private extern static bool dbus_connection_send (IntPtr  ptr,
                                                       IntPtr  message,
                                                       ref int client_serial);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_connection_flush")]
      private extern static void dbus_connection_flush (IntPtr  ptr);
    
    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_bus_get")]
      private extern static IntPtr dbus_bus_get (int        which,
                                                 ref Error  error);
    
    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_connection_get_data")]
      private extern static IntPtr dbus_connection_get_data (IntPtr ptr,
                                                             int    slot);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_connection_disconnect")]
      private extern static void dbus_connection_disconnect (IntPtr ptr);
    
    [DllImport (DBus.Internals.DBusGLibname, EntryPoint="dbus_connection_setup_with_g_main")]
      private extern static void dbus_connection_setup_with_g_main (IntPtr ptr,
                                                                    IntPtr context);
    
  }
}

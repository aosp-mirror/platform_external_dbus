namespace DBus {
  
  using System;
  using System.Runtime.InteropServices;
  using System.Diagnostics;
  
  public class Message {

    public Message (string name,
                    string dest_service) {
      // the assignment bumps the refcount
      raw = dbus_message_new (name, dest_service);
      if (raw == IntPtr.Zero)
        throw new OutOfMemoryException ();
      dbus_message_unref (raw);
    }

    public string Name {
      get {
        return dbus_message_get_name (raw);
      }
    }

    public static Message Wrap (IntPtr ptr) {
      IntPtr gch_ptr;
      
      gch_ptr = dbus_message_get_data (ptr, wrapper_slot);
      if (gch_ptr != IntPtr.Zero) {
        return (DBus.Message) ((GCHandle)gch_ptr).Target;
      } else {
        return new Message (ptr);
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
        
        if (raw_ != IntPtr.Zero) {
          IntPtr gch_ptr;
          
          gch_ptr = dbus_message_get_data (raw_,
                                           wrapper_slot);
          Debug.Assert (gch_ptr != IntPtr.Zero);

          dbus_message_set_data (raw_, wrapper_slot,
                                 IntPtr.Zero, IntPtr.Zero);
          
          ((GCHandle) gch_ptr).Free ();
          
          dbus_message_unref (raw_);
        }
        
        raw_ = value;

        if (raw_ != IntPtr.Zero) {
          GCHandle gch;

          dbus_message_ref (raw_);

          // We store a weak reference to the C# object on the C object
          gch = GCHandle.Alloc (this, GCHandleType.WeakTrackResurrection);
          
          dbus_message_set_data (raw_, wrapper_slot,
                                 (IntPtr) gch, IntPtr.Zero);
        }
      }
    }

    ~Message () {
      raw = IntPtr.Zero; // free the native object
    }
    
    Message (IntPtr r) {
      raw = r;
    }
    
    // static constructor runs before any methods 
    static Message () {
      DBus.Internals.Init ();
      
      Debug.Assert (wrapper_slot == -1);
      
      if (!dbus_message_allocate_data_slot (ref wrapper_slot))
        throw new OutOfMemoryException ();

      Debug.Assert (wrapper_slot >= 0);
    }

    // slot used to store the C# object on the C object
    static int wrapper_slot = -1;
    
    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_message_new")]
      private extern static IntPtr dbus_message_new (string name,
                                                     string dest_service);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_message_unref")]
      private extern static void dbus_message_unref (IntPtr ptr);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_message_ref")]
      private extern static void dbus_message_ref (IntPtr ptr);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_message_get_name")]
      private extern static string dbus_message_get_name (IntPtr ptr);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_message_allocate_data_slot")]
      private extern static bool dbus_message_allocate_data_slot (ref int slot);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_message_free_data_slot")]
      private extern static void dbus_message_free_data_slot (ref int slot);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_message_set_data")]
      private extern static bool dbus_message_set_data (IntPtr ptr,
                                                        int    slot,
                                                        IntPtr data,
                                                        IntPtr free_data_func);

    [DllImport (DBus.Internals.DBusLibname, EntryPoint="dbus_message_get_data")]
      private extern static IntPtr dbus_message_get_data (IntPtr ptr,
                                                          int    slot);
  }
}

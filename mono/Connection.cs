namespace DBus 
{
  
  using System;
  using System.Runtime.InteropServices;
  using System.Diagnostics;
  using System.Reflection;
  using System.IO;
  using System.Collections;
  
  public class Connection 
  {
    /// <summary>
    /// A pointer to the underlying Connection structure
    /// </summary>
    private IntPtr rawConnection;
    
    /// <summary>
    /// The current slot number
    /// </summary>
    private static int slot = -1;
    
    private int timeout = -1;

    internal Connection(IntPtr rawConnection)
    {
      RawConnection = rawConnection;
    }
    
    public Connection(string address)
    {
      // the assignment bumps the refcount
      Error error = new Error();
      error.Init();
      RawConnection = dbus_connection_open(address, ref error);
      if (RawConnection != IntPtr.Zero) {
	dbus_connection_unref(RawConnection);
      } else {
	throw new DBusException(error);
      }

      SetupWithMain();
    }

    public void Flush()
    {
      dbus_connection_flush(RawConnection);
    }

    public void SetupWithMain() 
    {      
      dbus_connection_setup_with_g_main(RawConnection, IntPtr.Zero);
    }
    
    ~Connection () 
    {
      if (RawConnection != IntPtr.Zero) 
	{
	  dbus_connection_disconnect(rawConnection);
	}
      RawConnection = IntPtr.Zero; // free the native object
    }
    
    internal static Connection Wrap(IntPtr rawConnection) 
    {
      if (slot > -1) {
	// Maybe we already have a Connection object associated with
	// this rawConnection then return it
	IntPtr rawThis = dbus_connection_get_data (rawConnection, slot);
	if (rawThis != IntPtr.Zero) {
	  return (DBus.Connection) ((GCHandle)rawThis).Target;
	}
      }
      
      // If it doesn't exist then create a new connection around it
      return new Connection(rawConnection);
    }

    public int Timeout
    {
      get
	{
	  return this.timeout;
	}
      set
	{
	  this.timeout = value;
	}
    }
    
    private int Slot
    {
      get 
	{
	  if (slot == -1) 
	    {
	      // We need to initialize the slot
	      if (!dbus_connection_allocate_data_slot (ref slot))
		throw new OutOfMemoryException ();
	      
	      Debug.Assert (slot >= 0);
	    }
	  
	  return slot;
	}
    }
    
    internal IntPtr RawConnection 
    {
      get 
	{
	  return rawConnection;
	}
      set 
	{
	  if (value == rawConnection)
	    return;
	  
	  if (rawConnection != IntPtr.Zero) 
	    {
	      // Get the reference to this
	      IntPtr rawThis = dbus_connection_get_data (rawConnection, Slot);
	      Debug.Assert (rawThis != IntPtr.Zero);
	      
	      // Blank over the reference
	      dbus_connection_set_data (rawConnection, Slot, IntPtr.Zero, IntPtr.Zero);
	      
	      // Free the reference
	      ((GCHandle) rawThis).Free();
	      
	      // Unref the connection
	      dbus_connection_unref(rawConnection);
	    }
	  
	  this.rawConnection = value;
	  
	  if (rawConnection != IntPtr.Zero) 
	    {
	      GCHandle rawThis;
	      
	      dbus_connection_ref (rawConnection);
	      
	      // We store a weak reference to the C# object on the C object
	      rawThis = GCHandle.Alloc (this, GCHandleType.WeakTrackResurrection);
	      
	      dbus_connection_set_data(rawConnection, Slot, (IntPtr) rawThis, IntPtr.Zero);
	    }
	}
    }

    [DllImport("dbus-glib-1")]
    private extern static void dbus_connection_setup_with_g_main(IntPtr rawConnection,
							     IntPtr rawContext);
    
    [DllImport ("dbus-1")]
    private extern static IntPtr dbus_connection_open (string address, ref Error error);
    
    [DllImport ("dbus-1")]
    private extern static void dbus_connection_unref (IntPtr ptr);
    
    [DllImport ("dbus-1")]
    private extern static void dbus_connection_ref (IntPtr ptr);
    
    [DllImport ("dbus-1")]
    private extern static bool dbus_connection_allocate_data_slot (ref int slot);
    
    [DllImport ("dbus-1")]
    private extern static void dbus_connection_free_data_slot (ref int slot);
    
    [DllImport ("dbus-1")]
    private extern static bool dbus_connection_set_data (IntPtr ptr,
							 int    slot,
							 IntPtr data,
							 IntPtr free_data_func);
    
    [DllImport ("dbus-1")]
    private extern static void dbus_connection_flush (IntPtr  ptr);
    
    [DllImport ("dbus-1")]
    private extern static IntPtr dbus_connection_get_data (IntPtr ptr,
							   int    slot);
    
    [DllImport ("dbus-1")]
    private extern static void dbus_connection_disconnect (IntPtr ptr);
  }
}

using System;
using System.Runtime.InteropServices;
using System.Reflection.Emit;

using DBus;

namespace DBus.DBusType
{
  /// <summary>
  /// An object path.
  /// </summary>
  public class ObjectPath : IDBusType
  {
    public const char Code = 'o';
    private string pathName = null;
    private object val = null;
    private Service service = null;
    
    private ObjectPath()
    {
    }
    
    public ObjectPath(object val) 
    {
      this.val = val;
    }
    
    public ObjectPath(IntPtr iter)
    {
      
      this.pathName = Marshal.PtrToStringAnsi(dbus_message_iter_get_object_path(iter));
    }

    public void SetService(Service service) 
    {
      this.service = service;
    }

    private string PathName 
    {
      get {
	if (this.pathName == null && this.val != null) {
	  Handler handler = this.service.GetHandler(this.val);
	  this.pathName = handler.PathName;
	}
	
	return this.pathName;
      }
    }

    public void Append(IntPtr iter) 
    {
      if (PathName == null) {
	throw new ApplicationException("Unable to append ObjectPath before calling SetService()");
      }
      
      if (!dbus_message_iter_append_object_path(iter, Marshal.StringToHGlobalAnsi(PathName)))
	throw new ApplicationException("Failed to append OBJECT_PATH argument:" + val);
    }

    public static bool Suits(System.Type type) 
    {
      object[] attributes = type.GetCustomAttributes(typeof(InterfaceAttribute), true);
      if (attributes.Length == 1) {
	return true;
      } else {
	return false;
      }
    }

    public static void EmitMarshalIn(ILGenerator generator, Type type)
    {
      if (type.IsByRef) {
	generator.Emit(OpCodes.Ldind_Ref);
      }
    }

    public static void EmitMarshalOut(ILGenerator generator, Type type, bool isReturn) 
    {
      generator.Emit(OpCodes.Castclass, type);
      if (!isReturn) {
	generator.Emit(OpCodes.Stind_Ref);
      }
    }

    public object Get() 
    {
      throw new ArgumentException("Cannot call Get on an ObjectPath without specifying type.");
    }

    public object Get(System.Type type)
    {
      if (this.service == null) {
	throw new ApplicationException("Unable to get ObjectPath before calling SetService()");
      }
      
      try {
	return this.service.GetObject(type, PathName);
      } catch(Exception ex) {
	throw new ArgumentException("Cannot cast object pointed to by Object Path to type '" + type.ToString() + "': " + ex);
      }
    }

    [DllImport("dbus-1")]
    private extern static IntPtr dbus_message_iter_get_object_path(IntPtr iter);
 
    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_append_object_path(IntPtr iter, IntPtr pathName);
  }
}

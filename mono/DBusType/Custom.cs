using System;
using System.Runtime.InteropServices;
using System.Reflection.Emit;

using DBus;

namespace DBus.DBusType
{
  /// <summary>
  /// A named byte array, used for custom types.
  /// </summary>
  public class Custom : IDBusType
  {
    public const char Code = 'c';
    private DBus.Custom val;
    
    private Custom()
    {
    }
    
    public Custom(DBus.Custom val, Service service) 
    {
      this.val = val;
    }

    public Custom(IntPtr iter, Service service)
    {
      string name;
      IntPtr value;
      int len;

      if (!dbus_message_iter_get_custom(iter, out name, out value, out len)) {
	throw new ApplicationException("Failed to get CUSTOM argument.");
      }

      this.val.Name = name;
      this.val.Data = new byte[len];
      Marshal.Copy(value, this.val.Data, 0, len);
    }
    
    public void Append(IntPtr iter)
    {
      IntPtr data = Marshal.AllocCoTaskMem(this.val.Data.Length);
      try {
	Marshal.Copy(this.val.Data, 0, data, this.val.Data.Length);
	if (!dbus_message_iter_append_custom(iter, this.val.Name, data, this.val.Data.Length)) {
	  throw new ApplicationException("Failed to append CUSTOM argument:" + val);
	}
      } finally {
	Marshal.FreeCoTaskMem(data);
      }
    }

    public static bool Suits(System.Type type) 
    {
      switch (type.ToString()) {
      case "DBus.Custom":
      case "DBus.Custom&":
	return true;
      }
      
      return false;
    }

    public static void EmitMarshalIn(ILGenerator generator, Type type)
    {
      if (type.IsByRef) {
	generator.Emit(OpCodes.Ldobj, type);
      }
    }

    public static void EmitMarshalOut(ILGenerator generator, Type type, bool isReturn) 
    {
      generator.Emit(OpCodes.Unbox, type);
      generator.Emit(OpCodes.Ldobj, type);
      if (!isReturn) {
	generator.Emit(OpCodes.Stobj, type);
      }
    }
    
    public object Get() 
    {
      return this.val;
    }

    public object Get(System.Type type)
    {
      switch (type.ToString()) {
      case "DBus.Custom":
      case "DBus.Custom&":
	return this.val;
      default:
	throw new ArgumentException("Cannot cast DBus.Type.Custom to type '" + type.ToString() + "'");
      }
    }

    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_get_custom(IntPtr iter,
							    out string name,
							    out IntPtr value,
							    out int len);
 
    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_append_custom(IntPtr iter, 
							       string name,
							       IntPtr data,
							       int len);
  }
}

using System;
using System.Runtime.InteropServices;
using System.Reflection.Emit;

using DBus;

namespace DBus.DBusType
{
  /// <summary>
  /// A string.
  /// </summary>
  public class String : IDBusType
  {
    public const char Code = 's';
    private string val;
    
    private String()
    {
    }
    
    public String(string val, Service service) 
    {
      this.val = val;
    }
    
    public String(IntPtr iter, Service service)
    {
      this.val = Marshal.PtrToStringAnsi(dbus_message_iter_get_string(iter));
    }

    public void Append(IntPtr iter) 
    {
      if (!dbus_message_iter_append_string(iter, Marshal.StringToHGlobalAnsi(val)))
	throw new ApplicationException("Failed to append STRING argument:" + val);
    }

    public static bool Suits(System.Type type) 
    {
      switch (type.ToString()) {
      case "System.String":
      case "System.String&":
	return true;
      }
      
      return false;
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
      return this.val;
    }

    public object Get(System.Type type)
    {
      switch (type.ToString()) 
	{
	case "System.String":
	case "System.String&":
	  return this.val;
	default:
	  throw new ArgumentException("Cannot cast DBus.Type.String to type '" + type.ToString() + "'");
	}
    }    

    [DllImport("dbus-1")]
    private extern static IntPtr dbus_message_iter_get_string(IntPtr iter);
 
    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_append_string(IntPtr iter, IntPtr value);
  }
}

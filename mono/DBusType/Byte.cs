using System;
using System.Runtime.InteropServices;
using System.Reflection.Emit;

using DBus;

namespace DBus.DBusType
{
  /// <summary>
  /// Byte
  /// </summary>
  public class Byte : IDBusType
  {
    public const char Code = 'y';
    private System.Byte val;
    
    private Byte()
    {
    }
    
    public Byte(System.Byte val) 
    {
      this.val = val;
    }

    public Byte(IntPtr iter)
    {
      this.val = dbus_message_iter_get_byte(iter);
    }
    
    public void Append(IntPtr iter)
    {
      if (!dbus_message_iter_append_byte(iter, val))
	throw new ApplicationException("Failed to append BYTE argument:" + val);
    }

    public static bool Suits(System.Type type) 
    {
      switch (type.ToString()) {
      case "System.Byte":
      case "System.Byte&":
	return true;
      }
      
      return false;
    }

    public static void EmitMarshalIn(ILGenerator generator, Type type)
    {
      if (type.IsByRef) {
	generator.Emit(OpCodes.Ldind_U1);
      }
    }

    public static void EmitMarshalOut(ILGenerator generator, Type type, bool isReturn) 
    {
      generator.Emit(OpCodes.Unbox, type);
      generator.Emit(OpCodes.Ldind_U1);
      if (!isReturn) {
	generator.Emit(OpCodes.Stind_I1);
      }
    }
    
    public object Get() 
    {
      return this.val;
    }

    public object Get(System.Type type)
    {
      switch (type.ToString()) {
      case "System.Byte":
      case "System.Byte&":
	return this.val;
      default:
	throw new ArgumentException("Cannot cast DBus.Type.Byte to type '" + type.ToString() + "'");
      }
    }

    [DllImport("dbus-1")]
    private extern static System.Byte dbus_message_iter_get_byte(IntPtr iter);
 
    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_append_byte(IntPtr iter, System.Byte value);
  }
}

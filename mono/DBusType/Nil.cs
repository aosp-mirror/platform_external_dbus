using System;
using System.Runtime.InteropServices;
using System.Reflection.Emit;

using DBus;

namespace DBus.DBusType
{
  /// <summary>
  /// Marks a "void"/"unset"/"nonexistent"/"null" argument.
  /// </summary>
  public class Nil : IDBusType
  {
    public const char Code = 'v';
    
    private Nil()
    {
    }
    
    public Nil(object nil, Service service) 
    {
    }

    public Nil(IntPtr iter, Service service)
    {
    }
    
    public void Append(IntPtr iter)
    {
      if (!dbus_message_iter_append_nil(iter))
	throw new ApplicationException("Failed to append NIL argument");
    }

    public static bool Suits(System.Type type) 
    {
      return false;
    }

    public static void EmitMarshalIn(ILGenerator generator, Type type)
    {
      if (type.IsByRef) {
	generator.Emit(OpCodes.Ldind_I1);
      }
    }

    public static void EmitMarshalOut(ILGenerator generator, Type type, bool isReturn) 
    {
      generator.Emit(OpCodes.Unbox, type);
      generator.Emit(OpCodes.Ldind_I1);
      if (!isReturn) {
	generator.Emit(OpCodes.Stind_I1);
      }
    }
    
    public object Get() 
    {
      return null;
    }

    public object Get(System.Type type)
    {
      throw new ArgumentException("Cannot cast DBus.Type.Nil to type '" + type.ToString() + "'");
    }

    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_append_nil(IntPtr iter);
  }
}

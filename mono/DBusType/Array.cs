using System;
using System.Collections;
using System.Runtime.InteropServices;
using System.Reflection.Emit;

using DBus;

namespace DBus.DBusType
{
  /// <summary>
  /// Array.
  /// </summary>
  public class Array : IDBusType
  {
    public const char Code = 'a';
    private System.Array val;
    private ArrayList elements;
    private Type elementType;
    private Service service = null;
    
    private Array()
    {
    }
    
    public Array(System.Array val, Service service) 
    {
      this.val = val;
      this.elementType = Arguments.MatchType(val.GetType().UnderlyingSystemType);
      this.service = service;
    }

    public Array(IntPtr iter, Service service)
    {
      this.service = service;

      IntPtr arrayIter = Marshal.AllocCoTaskMem(Arguments.DBusMessageIterSize);
      
      int elementTypeCode;
      bool notEmpty = dbus_message_iter_init_array_iterator(iter, arrayIter, out elementTypeCode);
      this.elementType = (Type) Arguments.DBusTypes[(char) elementTypeCode];

      elements = new ArrayList();

      if (notEmpty) {
	do {
	  object [] pars = new Object[2];
	  pars[0] = arrayIter;
	  pars[1] = service;
	  DBusType.IDBusType dbusType = (DBusType.IDBusType) Activator.CreateInstance(elementType, pars);
	  elements.Add(dbusType);
	} while (dbus_message_iter_next(arrayIter));
      }
      
      Marshal.FreeCoTaskMem(arrayIter);
    }
    
    public void Append(IntPtr iter)
    {
      IntPtr arrayIter = Marshal.AllocCoTaskMem(Arguments.DBusMessageIterSize);

      if (!dbus_message_iter_append_array(iter,
					  arrayIter,
					  (int) Arguments.GetCode(this.elementType))) {
	throw new ApplicationException("Failed to append INT32 argument:" + val);
      }

      foreach (object element in this.val) {
	object [] pars = new Object[2];
	pars[0] = element;
	pars[1] = this.service;
	DBusType.IDBusType dbusType = (DBusType.IDBusType) Activator.CreateInstance(elementType, pars);
	dbusType.Append(arrayIter);
      }

      Marshal.FreeCoTaskMem(arrayIter);
    }    

    public static bool Suits(System.Type type) 
    {
      if (type.IsArray) {
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
      throw new ArgumentException("Cannot call Get on an Array without specifying type.");
    }

    public object Get(System.Type type)
    {
      if (type.IsArray)
        type = type.GetElementType ();

      if (Arguments.Suits(elementType, type.UnderlyingSystemType)) {
	this.val = System.Array.CreateInstance(type.UnderlyingSystemType, elements.Count);
	int i = 0;
	foreach (DBusType.IDBusType element in elements) {
	  this.val.SetValue(element.Get(type.UnderlyingSystemType), i++);
	}	
      } else {
	throw new ArgumentException("Cannot cast DBus.Type.Array to type '" + type.ToString() + "'");
      }
	
	return this.val;
    }    

    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_init_array_iterator(IntPtr iter,
								     IntPtr arrayIter,
								     out int elementType);
 
    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_append_array(IntPtr iter, 
							      IntPtr arrayIter,
							      int elementType);

    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_has_next(IntPtr iter);

    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_next(IntPtr iter);
  }
}

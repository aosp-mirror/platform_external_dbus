using System;
using System.Collections;
using System.Runtime.InteropServices;
using System.Reflection.Emit;

using DBus;

namespace DBus.DBusType
{
  /// <summary>
  /// Dict.
  /// </summary>
  public class Dict : IDBusType
  {
    public const char Code = 'm';
    private Hashtable val;
    
    private Dict()
    {
    }
    
    public Dict(IDictionary val, Service service)
    {
      this.val = new Hashtable();
      foreach (DictionaryEntry entry in val) {
	this.val.Add(entry.Key, entry.Value);
      }
    }

    public Dict(IntPtr iter, Service service)
    {
      IntPtr dictIter = Marshal.AllocCoTaskMem(Arguments.DBusMessageIterSize);
      
      bool notEmpty = dbus_message_iter_init_dict_iterator(iter, dictIter);

      this.val = new Hashtable();

      if (notEmpty) {
	do {
	  string key = dbus_message_iter_get_dict_key(dictIter);
	  
	  // Get the argument type and get the value
	  Type elementType = (Type) DBus.Arguments.DBusTypes[(char) dbus_message_iter_get_arg_type(dictIter)];
	  object [] pars = new Object[1];
	  pars[0] = dictIter;
	  DBusType.IDBusType dbusType = (DBusType.IDBusType) Activator.CreateInstance(elementType, pars);
	  this.val.Add(key, dbusType);
	} while (dbus_message_iter_next(dictIter));
      }
      
      Marshal.FreeCoTaskMem(dictIter);
    }
    
    public void Append(IntPtr iter)
    {
      IntPtr dictIter = Marshal.AllocCoTaskMem(Arguments.DBusMessageIterSize);

      if (!dbus_message_iter_append_dict(iter,
					 dictIter)) {
	throw new ApplicationException("Failed to append DICT argument:" + val);
      }

      foreach (DictionaryEntry entry in this.val) {
	if (!dbus_message_iter_append_dict_key(dictIter, (string) entry.Key)) {
	  throw new ApplicationException("Failed to append DICT key:" + entry.Key);
	}
	
	// Get the element type
	Type elementType = Arguments.MatchType(entry.Value.GetType());
	object [] pars = new Object[1];
	pars[0] = entry.Value;
	DBusType.IDBusType dbusType = (DBusType.IDBusType) Activator.CreateInstance(elementType, pars);
	dbusType.Append(dictIter);
      }

      Marshal.FreeCoTaskMem(dictIter);
    }    

    public static bool Suits(System.Type type) 
    {
      if (typeof(IDictionary).IsAssignableFrom(type)) {
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
      return Get(typeof(Hashtable));
    }

    public object Get(System.Type type)
    {
      IDictionary retVal;

      if (Suits(type)) {
	retVal = (IDictionary) Activator.CreateInstance(type, new object[0]);
	foreach (DictionaryEntry entry in this.val) {
	  retVal.Add(entry.Key, ((IDBusType) entry.Value).Get());
	}
      } else {
	throw new ArgumentException("Cannot cast DBus.Type.Dict to type '" + type.ToString() + "'");
      }
	
      return retVal;
    }    

    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_init_dict_iterator(IntPtr iter,
								    IntPtr dictIter);
 
    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_append_dict(IntPtr iter, 
							     IntPtr dictIter);

    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_has_next(IntPtr iter);

    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_next(IntPtr iter);

    [DllImport("dbus-1")]
    private extern static string dbus_message_iter_get_dict_key (IntPtr dictIter);  

    [DllImport("dbus-1")]
    private extern static bool dbus_message_iter_append_dict_key (IntPtr dictIter,
								  string value);
    [DllImport("dbus-1")]
    private extern static int dbus_message_iter_get_arg_type(IntPtr iter);
  }
}

namespace DBus
{
  using System;
  using System.Runtime.InteropServices;
  using System.Diagnostics;
  using System.Collections;
  using System.Reflection;
  using System.Reflection.Emit;
  
  public class Service
  {
    private Connection connection;
    private string name;
    private bool local = false;
    private Hashtable registeredHandlers = new Hashtable();
    internal ModuleBuilder module = null;

    internal Service(string name, Connection connection)
    {
      this.name = name;
      this.connection = connection;
    }

    public Service(Connection connection, string name)
    {
      Error error = new Error();
      error.Init();
      
      // This isn't used for now
      uint flags = 0;

      if (dbus_bus_acquire_service(connection.RawConnection, name, flags, ref error) == -1) {
	throw new DBusException(error);
      }

      this.connection = connection;
      this.name = name;
      this.local = true;
    }

    public static bool Exists(Connection connection, string name)
    {
      Error error = new Error();
      error.Init();
      
      if (dbus_bus_service_exists(connection.RawConnection, 
				  name, 
				  ref error)) {
	return true;
      } else {
	if (error.IsSet) {
	  throw new DBusException(error);
	}
	return false;
      }
    }

    public static Service Get(Connection connection, string name)
    {
      if (Exists(connection, name)) {
	return new Service(name, connection);
      } else {
	throw new ApplicationException("Service '" + name + "' does not exist.");
      }
    }

    public void RegisterObject(object handledObject, 
			       string pathName) 
    {
      Handler handler = new Handler(handledObject, 
				    pathName, 
				    this);
      registeredHandlers.Add(handledObject, handler);
    }

    internal Handler GetHandler(object handledObject) 
    {
      if (!registeredHandlers.Contains(handledObject)) {
	throw new ArgumentException("No handler registered for object: " + handledObject);
      }
      
      return (Handler) registeredHandlers[handledObject];
    }

    public object GetObject(Type type, string pathName)
    {
      ProxyBuilder builder = new ProxyBuilder(this, type, pathName);
      object proxy = builder.GetProxy();
      return proxy;
    }

    public string Name
    {
      get
	{
	  return this.name;
	}
    }

    public Connection Connection 
    {
      get
	{
	  return connection;
	}
      
      set 
	{
	  this.connection = value;
	}
    }

    [DllImport ("dbus-1")]
    private extern static int dbus_bus_acquire_service (IntPtr rawConnection, string serviceName, uint flags, ref Error error);

    [DllImport ("dbus-1")]
    private extern static bool dbus_bus_service_exists (IntPtr rawConnection, string serviceName, ref Error error);    
  }
}

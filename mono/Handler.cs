namespace DBus
{
  using System;
  using System.Runtime.InteropServices;
  using System.Diagnostics;
  using System.Reflection;
  using System.Collections;

  internal enum Result 
  {
    Handled = 0,
    NotYetHandled = 1,
    NeedMemory = 2
  }

  internal class Handler : IDisposable
  {
    private string[] path = null;
    private string pathName = null;
    private Introspector introspector = null;
    private object handledObject = null;
    private DBusObjectPathVTable vTable;
    private Connection connection;
    private Service service;
    private bool disposed = false;
    
    internal delegate void DBusObjectPathUnregisterFunction(IntPtr rawConnection,
							    IntPtr userData);

    internal delegate int DBusObjectPathMessageFunction(IntPtr rawConnection,
							IntPtr rawMessage,
							IntPtr userData);

    [StructLayout (LayoutKind.Sequential)]
    private struct DBusObjectPathVTable 
    {
      public DBusObjectPathUnregisterFunction unregisterFunction;
      public DBusObjectPathMessageFunction messageFunction;
      public IntPtr padding1;
      public IntPtr padding2;
      public IntPtr padding3;
      public IntPtr padding4;
    
      public DBusObjectPathVTable(DBusObjectPathUnregisterFunction unregisterFunction,
				  DBusObjectPathMessageFunction messageFunction) 
      {
	this.unregisterFunction = unregisterFunction;
	this.messageFunction = messageFunction;
	this.padding1 = IntPtr.Zero;
	this.padding2 = IntPtr.Zero;
	this.padding3 = IntPtr.Zero;
	this.padding4 = IntPtr.Zero;
      }
    }

    public void Dispose() 
    {
      Dispose(true);
      GC.SuppressFinalize(this);
    }
    
    private void Dispose(bool disposing) 
    {
      if (!disposed) {	
	if (disposing) {
	  // Clean up managed resources
	}

	service = null;

	// Clean up unmanaged resources
	if (Connection != null && Connection.RawConnection != IntPtr.Zero && path != null) {
	  dbus_connection_unregister_object_path(Connection.RawConnection,
						 Path);
	}	

	connection = null;
	introspector = null;
	handledObject = null;
      }
      
      disposed = true;    
    }

    ~Handler() 
    {
      Dispose(false);
    }

    public Handler(object handledObject, 
		   string pathName, 
		   Service service)
    {
      Service = service;
      Connection = service.Connection;
      HandledObject = handledObject;

      // Strip the leading / off if there is one and get the path as an array
      pathName = pathName.TrimStart('/');
      this.path = pathName.Split('/');
      this.pathName = "/" + pathName;
      
      // Create the vTable and register the path
      vTable = new DBusObjectPathVTable(new DBusObjectPathUnregisterFunction(Unregister_Called), 
					new DBusObjectPathMessageFunction(Message_Called));
      
      if (!dbus_connection_register_object_path(Connection.RawConnection,
						Path,
						ref vTable,
						IntPtr.Zero))
	throw new OutOfMemoryException();

      RegisterSignalHandlers();
    }

    private void RegisterSignalHandlers()
    {
      ProxyBuilder proxyBuilder = new ProxyBuilder(Service, HandledObject.GetType(), this.pathName);

      foreach (DictionaryEntry interfaceEntry in this.introspector.InterfaceProxies) {
	InterfaceProxy interfaceProxy = (InterfaceProxy) interfaceEntry.Value;
	foreach (DictionaryEntry signalEntry in interfaceProxy.Signals) {
	  EventInfo eventE = (EventInfo) signalEntry.Value;
	  Delegate del = Delegate.CreateDelegate(eventE.EventHandlerType, proxyBuilder.GetSignalProxy(), "Proxy_" + eventE.Name);
	  eventE.AddEventHandler(HandledObject, del);
	}
      }
    }

    public object HandledObject 
    {
      get {
	return this.handledObject;
      }
      
      set {
	this.handledObject = value;
	
	object[] attributes;
	
	// Register the methods
	this.introspector = Introspector.GetIntrospector(value.GetType());	  
      }
    }

    public void Unregister_Called(IntPtr rawConnection, 
				  IntPtr userData)
    {
      if (service != null) {
	service.UnregisterObject(HandledObject);
      }

      path = null;
    }

    private int Message_Called(IntPtr rawConnection, 
			       IntPtr rawMessage, 
			       IntPtr userData) 
    {
      Message message = Message.Wrap(rawMessage, Service);

      switch (message.Type) {
      case Message.MessageType.Signal:
	// We're not interested in signals here because we're the ones
	// that generate them!
	break;
      case Message.MessageType.MethodCall:
	return (int) HandleMethod((MethodCall) message);
      }

      return (int) Result.NotYetHandled;
    }
    
    private Result HandleMethod(MethodCall methodCall)
    {
      methodCall.Service = service;
      
      InterfaceProxy interfaceProxy = this.introspector.GetInterface(methodCall.InterfaceName);
      if (interfaceProxy == null || !interfaceProxy.HasMethod(methodCall.Key)) {
	// No such interface here.
	return Result.NotYetHandled;
      }
      
      MethodInfo method = interfaceProxy.GetMethod(methodCall.Key);

      // Now call the method. FIXME: Error handling
      object [] args = methodCall.Arguments.GetParameters(method);
      object retVal = method.Invoke(this.handledObject, args);

      // Create the reply and send it
      MethodReturn methodReturn = new MethodReturn(methodCall);
      methodReturn.Arguments.AppendResults(method, retVal, args);
      methodReturn.Send();

      return Result.Handled;
    }

    internal string[] Path 
    {
      get 
	{
	  return path;
	}
    }

    public string PathName
    {
      get
	{
	  return pathName;
	}
    }

    internal Connection Connection 
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

    public Service Service
    {
      get
	{
	  return service;
	}
      
      set 
	{
	  this.service = value;
	}
    }

    [DllImport ("dbus-1")]
    private extern static bool dbus_connection_register_object_path (IntPtr rawConnection, string[] path, ref DBusObjectPathVTable vTable, IntPtr userData);

    [DllImport ("dbus-1")]
    private extern static void dbus_connection_unregister_object_path (IntPtr rawConnection, string[] path);

  }
}

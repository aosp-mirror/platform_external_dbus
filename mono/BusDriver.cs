namespace DBus
{

  using System;

  public delegate void ServiceEventHandler (string serviceName);

  [Interface ("org.freedesktop.DBus")]
  public abstract class BusDriver
  {
    [Method]
    public abstract string[] ListServices ();

    [Method]
    public abstract string GetServiceOwner (string serviceName);

    [Method]
    public abstract UInt32 GetConnectionUnixUser (string connectionName);


    [Signal]
    public virtual event ServiceEventHandler ServiceCreated;

    [Signal]
    public virtual event ServiceEventHandler ServiceDeleted;


    static public BusDriver New (Connection connection)
    {
      Service service;
      service = Service.Get (connection, "org.freedesktop.DBus");

      BusDriver driver;
      driver = (BusDriver) service.GetObject (typeof (BusDriver), "/org/freedesktop/DBus");
      
      return driver;
    }
  }
}

namespace DBus
{
  using System;
  using System.Runtime.InteropServices;
  using System.Diagnostics;
  
  public class Signal : Message
  {    
    public Signal() : base(MessageType.Signal)
    {  
    }

    internal Signal(IntPtr rawMessage, Service service) : base(rawMessage, service)
    {
    }

    public Signal(Service service) : base(MessageType.Signal, service) 
    {
    }

    public new string PathName
    {
      get
	{
	  return base.PathName;
	}

      set
	{
	  base.PathName = value;
	}
    }

    public new string InterfaceName
    {
      get
	{
	  return base.InterfaceName;
	}

      set
	{
	  base.InterfaceName = value;
	}
    }

    public new string Name
    {
      get
	{
	  return base.Name;
	}

      set
	{
	  base.Name = value;
	}
    }
  }
}

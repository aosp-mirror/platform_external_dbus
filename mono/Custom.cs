using System;

using DBus;

namespace DBus
{
  public struct Custom
  {
    public string Name;
    public byte[] Data;
    
    public Custom(string name, byte[] data) 
    {
      Name = name;
      Data = data;
    }
  }
}

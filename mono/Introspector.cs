namespace DBus 
{
  
  using System;
  using System.Runtime.InteropServices;
  using System.Diagnostics;
  using System.Collections;
  using System.Reflection;
  
  internal class Introspector
  {
    private Type type;
    private string interfaceName;
    
    public Introspector(Type type)    {
      object[] attributes = type.GetCustomAttributes(typeof(InterfaceAttribute), true);
      if (attributes.Length != 1)
	throw new ApplicationException("Type '" + type + "' is not a D-BUS interface.");
      
      InterfaceAttribute interfaceAttribute = (InterfaceAttribute) attributes[0];
     
      this.interfaceName = interfaceAttribute.InterfaceName;
      this.type = type;
    }
    
    public string InterfaceName
    {
      get
	{
	  return this.interfaceName;
	}
    }

    public ConstructorInfo Constructor
    {
      get
	{
	  ConstructorInfo ret = this.type.GetConstructor(new Type[0]);
	  if (ret != null) {
	    return ret;
	  } else {
	    return typeof(object).GetConstructor(new Type[0]);
	  }
	}
    }

    public IntrospectorMethods Methods
    {
      get
	{
	  return new IntrospectorMethods(this.type);
	}
    }

    public class IntrospectorMethods : IEnumerable
    {
      private Type type;
      
      public IntrospectorMethods(Type type)
      {
	this.type = type;
      }

      public IEnumerator GetEnumerator()
      {
	return new MethodEnumerator(this.type.GetMethods(BindingFlags.Public|BindingFlags.Instance).GetEnumerator());
      }

      private class MethodEnumerator : IEnumerator
      {
	private IEnumerator enumerator;
	
	public MethodEnumerator(IEnumerator enumerator)
	{
	  this.enumerator = enumerator;
	}
	
	public bool MoveNext()
	{
	  while (enumerator.MoveNext()) {
	    MethodInfo method = (MethodInfo) enumerator.Current;
	    object[] attributes = method.GetCustomAttributes(typeof(MethodAttribute), true);
	    if (attributes.GetLength(0) > 0) {
	      return true;
	    }
	  }
	  
	  return false;
	}
	
	public void Reset()
	{
	  enumerator.Reset();
	}
	
	public object Current
	{
	  get
	    {
	      return enumerator.Current;
	    }
	}
      }
    }
  }
}

namespace DBus
{
  using System;
  using System.Collections;
  using System.Reflection;
  
  internal class InterfaceProxy
  {
    private static Hashtable interfaceProxies = new Hashtable();
    private Hashtable methods = null;

    private string interfaceName;

    private InterfaceProxy(Type type) 
    {
      object[] attributes = type.GetCustomAttributes(typeof(InterfaceAttribute), true);
      InterfaceAttribute interfaceAttribute = (InterfaceAttribute) attributes[0];
      this.interfaceName = interfaceAttribute.InterfaceName;
      AddMethods(type);
    }

    private void AddMethods(Type type)
    {
      this.methods = new Hashtable();
      foreach (MethodInfo method in type.GetMethods(BindingFlags.Public | 
						    BindingFlags.Instance | 
						    BindingFlags.DeclaredOnly)) {
	object[] attributes = method.GetCustomAttributes(typeof(MethodAttribute), true);
	if (attributes.GetLength(0) > 0) {
	  methods.Add(GetKey(method), method);
	}
      }
    }
    

    public static InterfaceProxy GetInterface(Type type) 
    {
      if (!interfaceProxies.Contains(type)) {
	interfaceProxies[type] = new InterfaceProxy(type);
      }

      return (InterfaceProxy) interfaceProxies[type];
    }

    public bool HasMethod(string key) 
    {
      return this.Methods.Contains(key);
    }
    
    public MethodInfo GetMethod(string key)
    {
      return (MethodInfo) this.Methods[key];
    }

    private string GetKey(MethodInfo method) 
    {
      ParameterInfo[] pars = method.GetParameters();
      string key = method.Name + " ";
      
      foreach (ParameterInfo par in pars) {
	if (!par.IsOut) {
	  Type dbusType = Arguments.MatchType(par.ParameterType);
	  key += Arguments.GetCode(dbusType);
	}
      }

      return key;
    }

    public Hashtable Methods
    {
      get {
	return this.methods;
      }
    }
    
    public string InterfaceName
    {
      get {
	return this.interfaceName;
      }
    }
  }
}

    

namespace DBus
{
  using System;
  using System.Runtime.InteropServices;
  using System.Diagnostics;
  using System.Collections;
  using System.Threading;
  using System.Reflection;
  using System.Reflection.Emit;

  internal class ProxyBuilder
  {
    private Service service= null;
    private string pathName = null;
    private Type type = null;
    private Introspector introspector = null;
    private AssemblyBuilder proxyAssembly;
    
    private static MethodInfo Service_NameMI = typeof(Service).GetMethod("get_Name", 
									    new Type[0]);
    private static MethodInfo Service_ConnectionMI = typeof(Service).GetMethod("get_Connection",
										  new Type[0]);
    private static MethodInfo Message_ArgumentsMI = typeof(Message).GetMethod("get_Arguments",
										 new Type[0]);
    private static MethodInfo Arguments_InitAppendingMI = typeof(Arguments).GetMethod("InitAppending",
											  new Type[0]);
    private static MethodInfo Arguments_AppendMI = typeof(Arguments).GetMethod("Append",
										  new Type[] {typeof(DBusType.IDBusType)});
    private static MethodInfo Message_SendWithReplyAndBlockMI = typeof(Message).GetMethod("SendWithReplyAndBlock",
											     new Type[0]);
    private static MethodInfo Arguments_GetEnumeratorMI = typeof(Arguments).GetMethod("GetEnumerator",
											  new Type[0]);
    private static MethodInfo IEnumerator_MoveNextMI = typeof(System.Collections.IEnumerator).GetMethod("MoveNext",
													new Type[0]);
    private static MethodInfo IEnumerator_CurrentMI = typeof(System.Collections.IEnumerator).GetMethod("get_Current",
												       new Type[0]);
    private static MethodInfo Type_GetTypeFromHandleMI = typeof(System.Type).GetMethod("GetTypeFromHandle",
										       new Type[] {typeof(System.RuntimeTypeHandle)});
    private static MethodInfo IDBusType_GetMI = typeof(DBusType.IDBusType).GetMethod("Get",
										     new Type[] {typeof(System.Type)});
    private static ConstructorInfo MethodCall_C = typeof(MethodCall).GetConstructor(new Type[] {typeof(Service),
												typeof(string),
												typeof(string),
												typeof(string)});
    
											

    public ProxyBuilder(Service service, Type type, string pathName)
    {
      this.service = service;
      this.pathName = pathName;
      this.type = type;
      this.introspector = Introspector.GetIntrospector(type);
    }

    private void BuildMethod(MethodInfo method, 
			     InterfaceProxy interfaceProxy,
			     ref TypeBuilder typeB, 
			     FieldInfo serviceF,
			     FieldInfo pathF)
    {
      ParameterInfo[] pars = method.GetParameters();
      Type[] parTypes = new Type[pars.Length];
      for (int parN = 0; parN < pars.Length; parN++) {
	parTypes[parN] = pars[parN].ParameterType;
      }

      // Generate the code
      MethodBuilder methodBuilder = typeB.DefineMethod(method.Name, 
						       MethodAttributes.Public |
						       MethodAttributes.HideBySig |
						       MethodAttributes.Virtual, 
						       method.ReturnType, 
						       parTypes);
      ILGenerator generator = methodBuilder.GetILGenerator();

      for (int parN = 0; parN < pars.Length; parN++) {
	methodBuilder.DefineParameter(parN + 1, pars[parN].Attributes, pars[parN].Name);
      }

      // Generate the locals
      LocalBuilder methodCallL = generator.DeclareLocal(typeof(MethodCall));
      methodCallL.SetLocalSymInfo("methodCall");
      LocalBuilder replyL = generator.DeclareLocal(typeof(MethodReturn));
      replyL.SetLocalSymInfo("reply");
      LocalBuilder enumeratorL = generator.DeclareLocal(typeof(System.Collections.IEnumerator));
      enumeratorL.SetLocalSymInfo("enumerator");

      if (method.ReturnType != typeof(void)) {
	LocalBuilder retvalL = generator.DeclareLocal(method.ReturnType);
	retvalL.SetLocalSymInfo("retval");
      }

      //generator.EmitWriteLine("MethodCall methodCall = new MethodCall(...)");
      generator.Emit(OpCodes.Ldsfld, serviceF);
      generator.Emit(OpCodes.Ldarg_0);
      generator.Emit(OpCodes.Ldfld, pathF);
      generator.Emit(OpCodes.Ldstr, interfaceProxy.InterfaceName);
      generator.Emit(OpCodes.Ldstr, method.Name);
      generator.Emit(OpCodes.Newobj, MethodCall_C);
      generator.Emit(OpCodes.Stloc_0);

      //generator.EmitWriteLine("methodCall.Arguments.InitAppending()");
      generator.Emit(OpCodes.Ldloc_0);
      generator.EmitCall(OpCodes.Callvirt, Message_ArgumentsMI, null);
      generator.EmitCall(OpCodes.Callvirt, Arguments_InitAppendingMI, null);

      for (int parN = 0; parN < pars.Length; parN++) {
	ParameterInfo par = pars[parN];
	if (!par.IsOut) {
	  EmitIn(generator, par.ParameterType, parN);
	}
      }
      
      //generator.EmitWriteLine("MethodReturn reply = methodCall.SendWithReplyAndBlock()");
      generator.Emit(OpCodes.Ldloc_0);
      generator.EmitCall(OpCodes.Callvirt, Message_SendWithReplyAndBlockMI, null);      
      generator.Emit(OpCodes.Stloc_1);

      //generator.EmitWriteLine("IEnumerator enumeartor = reply.Arguments.GetEnumerator()");
      generator.Emit(OpCodes.Ldloc_1);
      generator.EmitCall(OpCodes.Callvirt, Message_ArgumentsMI, null);
      generator.EmitCall(OpCodes.Callvirt, Arguments_GetEnumeratorMI, null);
      generator.Emit(OpCodes.Stloc_2);

      // handle the return value
      if (method.ReturnType != typeof(void)) {
	EmitOut(generator, method.ReturnType, 0);
      }

      for (int parN = 0; parN < pars.Length; parN++) {
	ParameterInfo par = pars[parN];
	if (par.IsOut || par.ParameterType.ToString().EndsWith("&")) {
	  EmitOut(generator, par.ParameterType, parN);
	}
      }

      if (method.ReturnType != typeof(void)) {
	generator.Emit(OpCodes.Ldloc_3);
      }
      
      generator.Emit(OpCodes.Ret);

      // Generate the method
      typeB.DefineMethodOverride(methodBuilder, method);
    }

    private void EmitIn(ILGenerator generator, Type parType, int parN)
    {
      Type inParType = Arguments.MatchType(parType);
      //generator.EmitWriteLine("methodCall.Arguments.Append(...)");
      generator.Emit(OpCodes.Ldloc_0);
      generator.EmitCall(OpCodes.Callvirt, Message_ArgumentsMI, null);
      generator.Emit(OpCodes.Ldarg_S, parN + 1);

      // Call the DBusType EmitMarshalIn to make it emit itself
      object[] pars = new object[] {generator, parType};
      inParType.InvokeMember("EmitMarshalIn", BindingFlags.Static | BindingFlags.Public | BindingFlags.InvokeMethod, null, null, pars, null);

      generator.Emit(OpCodes.Newobj, Arguments.GetDBusTypeConstructor(inParType, parType));
      generator.EmitCall(OpCodes.Callvirt, Arguments_AppendMI, null);
    }

    private void EmitOut(ILGenerator generator, Type parType, int parN)
    {
      Type outParType = Arguments.MatchType(parType);
      //generator.EmitWriteLine("enumerator.MoveNext()");
      generator.Emit(OpCodes.Ldloc_2);
      generator.EmitCall(OpCodes.Callvirt, IEnumerator_MoveNextMI, null);

      //generator.EmitWriteLine("return (" + parType + ") ((DBusType.IDBusType) enumerator.Current).Get(typeof(" + parType + "))");
      generator.Emit(OpCodes.Pop);
      if (parN > 0) {
	generator.Emit(OpCodes.Ldarg_S, parN + 1);
      }
      
      generator.Emit(OpCodes.Ldloc_2);
      generator.EmitCall(OpCodes.Callvirt, IEnumerator_CurrentMI, null);
      generator.Emit(OpCodes.Castclass, typeof(DBusType.IDBusType));
      generator.Emit(OpCodes.Ldtoken, parType);
      generator.EmitCall(OpCodes.Call, Type_GetTypeFromHandleMI, null);
      generator.EmitCall(OpCodes.Callvirt, IDBusType_GetMI, null);

      // Call the DBusType EmitMarshalOut to make it emit itself
      object[] pars = new object[] {generator, parType, parN == 0};
      outParType.InvokeMember("EmitMarshalOut", BindingFlags.Static | BindingFlags.Public | BindingFlags.InvokeMethod, null, null, pars, null);
      
      if (parN == 0) {
	generator.Emit(OpCodes.Stloc_3);
      }
    }
    
    public void BuildConstructor(ref TypeBuilder typeB, FieldInfo serviceF, FieldInfo pathF)
    {
      Type[] pars = {typeof(Service), typeof(string)};
      ConstructorBuilder constructor = typeB.DefineConstructor(MethodAttributes.RTSpecialName | 
							       MethodAttributes.Public,
							       CallingConventions.Standard, pars);

      ILGenerator generator = constructor.GetILGenerator();
      generator.Emit(OpCodes.Ldarg_0);
      generator.Emit(OpCodes.Call, this.introspector.Constructor);
      generator.Emit(OpCodes.Ldarg_1);
      generator.Emit(OpCodes.Stsfld, serviceF);
      generator.Emit(OpCodes.Ldarg_0);
      generator.Emit(OpCodes.Ldarg_2);
      generator.Emit(OpCodes.Stfld, pathF);

      generator.Emit(OpCodes.Ret);
    }
    
    public object GetProxy() 
    {      
      
      // Build the type
      TypeBuilder typeB = ServiceModuleBuilder.DefineType(ProxyName, TypeAttributes.Public, this.type);
      
      FieldBuilder serviceF = typeB.DefineField("service", 
						typeof(Service), 
						FieldAttributes.Private | 
						FieldAttributes.Static);
      FieldBuilder pathF = typeB.DefineField("pathName", 
					     typeof(string), 
					     FieldAttributes.Private);

      BuildConstructor(ref typeB, serviceF, pathF);

      // Build the methods
      foreach (DictionaryEntry interfaceEntry in this.introspector.InterfaceProxies) {
	InterfaceProxy interfaceProxy = (InterfaceProxy) interfaceEntry.Value;
	foreach (DictionaryEntry methodEntry in interfaceProxy.Methods) {
	  MethodInfo method = (MethodInfo) methodEntry.Value;
	  BuildMethod(method, interfaceProxy, ref typeB, serviceF, pathF);
	}
      }
      
      Type [] parTypes = new Type[] {typeof(Service), typeof(string)};
      object [] pars = new object[] {Service, pathName};
      
      Type proxyType = typeB.CreateType();

      // Uncomment the following line to produce a DLL of the
      // constructed assembly which can then be examined using
      // monodis. Note that in order for this to work you should copy
      // the client assembly as a dll file so that monodis can pick it
      // up.
      ProxyAssembly.Save("proxy.dll");

      ConstructorInfo constructor = proxyType.GetConstructor(parTypes);
      object instance = constructor.Invoke(pars);
      return instance;
    }

    private ModuleBuilder ServiceModuleBuilder
    {
      get {
	if (Service.module == null) {
	  Service.module = ProxyAssembly.DefineDynamicModule(Service.Name, "proxy.dll", true);
	}
	
	return Service.module;
      }
    }
  
  private Service Service
    {
      get {
	return this.service;
      }
    }

    private string ProxyName
    {
      get {
	return this.introspector.ToString() + ".Proxy";
      }
    }

    private AssemblyBuilder ProxyAssembly
    {
      get {
	if (this.proxyAssembly == null){
	  AssemblyName assemblyName = new AssemblyName();
	  assemblyName.Name = "DBusProxy";
	  this.proxyAssembly = Thread.GetDomain().DefineDynamicAssembly(assemblyName, 
									AssemblyBuilderAccess.RunAndSave);
	}
	
	return this.proxyAssembly;
      }
    }
  }
}


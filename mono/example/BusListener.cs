namespace Foo
{
	using System;
	using DBus;
	using Gtk;

	public class BusListener
	{

		static void OnServiceOwnerChanged (string serviceName,
						   string oldOwner,
						   string newOwner)
		{
			if (oldOwner == "")
				Console.WriteLine ("{0} created by {1}",
						   serviceName, newOwner);
			else if (newOwner == "")
				Console.WriteLine ("{0} released by {1}", 
						   serviceName, oldOwner);
			else
				Console.WriteLine ("{0} transfered from {1} to {2}",
						   serviceName, oldOwner, newOwner);
		}

		public static int Main (string [] args)
		{
			Application.Init ();

			Connection connection;
			connection = Bus.GetSessionBus ();

			BusDriver driver = BusDriver.New (connection);
			driver.ServiceOwnerChanged += new ServiceEventHandler (OnServiceOwnerChanged);

			Console.WriteLine ("Listening for service changes...");

			Application.Run ();

			return 0;
		}
	}

	
}

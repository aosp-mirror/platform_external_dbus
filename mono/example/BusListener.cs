namespace Foo
{
	using System;
	using DBus;
	using Gtk;

	public class BusListener
	{

		static void OnNameOwnerChanged (string name,
						string oldOwner,
						string newOwner)
		{
			if (oldOwner == "")
				Console.WriteLine ("{0} created by {1}",
						   name, newOwner);
			else if (newOwner == "")
				Console.WriteLine ("{0} released by {1}", 
						   name, oldOwner);
			else
				Console.WriteLine ("{0} transfered from {1} to {2}",
						   name, oldOwner, newOwner);
		}

		public static int Main (string [] args)
		{
			Application.Init ();

			Connection connection;
			connection = Bus.GetSessionBus ();

			BusDriver driver = BusDriver.New (connection);
			driver.NameOwnerChanged += new NameOwnerChangedHandler (OnNameOwnerChanged);

			Console.WriteLine ("Listening for name owner changes...");

			Application.Run ();

			return 0;
		}
	}

	
}

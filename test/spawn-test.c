#include <dbus/dbus.h>

#define DBUS_COMPILATION /* cheat and use dbus-sysdeps */
#include <dbus/dbus-sysdeps.h>
#undef DBUS_COMPILATION
#include <stdio.h>

int
main (int argc, char **argv)
{
  char **argv_copy;
  int i;
  DBusError error;
  
  if (argc < 2)
    {
      fprintf (stderr, "You need to specify a program to launch.\n");

      return -1;
    }

  argv_copy = dbus_new (char *, argc);
  for (i = 0; i < argc - 1; i++)
    argv_copy [i] = argv[i + 1];
  argv_copy[argc - 1] = NULL;
  
  if (!_dbus_spawn_async (argv_copy, &error))
    {
      fprintf (stderr, "Could not launch application: \"%s\"\n",
	       error.message);
    }
  
  return 0;
}

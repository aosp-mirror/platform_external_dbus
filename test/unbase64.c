#include <dbus/dbus.h>
#define DBUS_COMPILATION /* cheat and use string etc. */
#include <dbus/dbus-string.h>
#include <dbus/dbus-sysdeps.h>
#include <dbus/dbus-internals.h>
#undef DBUS_COMPILATION
#include <stdio.h>

int
main (int    argc,
      char **argv)
{
  DBusString contents;
  DBusString decoded;
  DBusString filename;
  const char *s;
  DBusError error;
  
  if (argc < 2)
    {
      fprintf (stderr, "Give the file to decode as an argument\n");
      return 1;
    }

  _dbus_string_init_const (&filename, argv[1]);
  
  if (!_dbus_string_init (&contents))
    return 1;

  if (!_dbus_string_init (&decoded))
    return 1;

  dbus_error_init (&error);
  if (!_dbus_file_get_contents (&contents, &filename, &error))
    {
      fprintf (stderr, "Failed to load file: %s\n", error.message);
      dbus_error_free (&error);
      return 1;
    }

  if (!_dbus_string_base64_decode (&contents, 0,
                                   &decoded, 0))
    return 1;

  s = _dbus_string_get_const_data (&decoded);
  
  fputs (s, stdout);
  
  return 0;
}

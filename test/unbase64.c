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
  
  if (argc < 2)
    {
      fprintf (stderr, "Give the file to decode as an argument\n");
      return 1;
    }

  _dbus_string_init_const (&filename, argv[1]);
  
  if (!_dbus_string_init (&contents, _DBUS_INT_MAX))
    return 1;

  if (!_dbus_string_init (&decoded, _DBUS_INT_MAX))
    return 1;

  if (_dbus_file_get_contents (&contents, &filename) != DBUS_RESULT_SUCCESS)
    return 1;

  if (!_dbus_string_base64_decode (&contents, 0,
                                   &decoded, 0))
    return 1;

  _dbus_string_get_const_data (&decoded, &s);
  
  fputs (s, stdout);
  
  return 0;
}

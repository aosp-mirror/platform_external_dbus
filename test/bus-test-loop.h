#include <dbus/dbus.h>

void bus_test_loop_hookup_with_server     (DBusServer     *server);
void bus_test_loop_hookup_with_connection (DBusConnection *connection);

void bus_test_loop_quit (void);
void bus_test_loop_run (void);




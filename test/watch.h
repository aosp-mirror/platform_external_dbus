/* Cheesy main loop thingy used by the test programs */

#ifndef WATCH_H
#define WATCH_H

#include <dbus/dbus.h>

void do_mainloop (void);

void quit_mainloop (void);

void setup_connection (DBusConnection *connection);
void setup_server     (DBusServer *server);

#endif

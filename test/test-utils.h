#ifndef TEST_UTILS_H
#define TEST_UTILS_H
#include <config.h>
#define DBUS_COMPILATION /* Cheat and use private stuff */
#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <dbus/dbus-mainloop.h>
#include <dbus/dbus-internals.h>
#undef DBUS_COMPILATION

dbus_bool_t test_connection_setup                 (DBusLoop       *loop,
                                                   DBusConnection *connection);
void        test_connection_shutdown              (DBusLoop       *loop,
                                                   DBusConnection *connection);
void        test_connection_dispatch_all_messages (DBusConnection *connection);
dbus_bool_t test_connection_dispatch_one_message  (DBusConnection *connection);


#endif

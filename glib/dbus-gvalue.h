#ifndef DBUS_GOBJECT_VALUE_H
#define DBUS_GOBJECT_VALUE_H

#include <dbus/dbus.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

gboolean dbus_gvalue_demarshal (DBusMessageIter *iter, GValue *value);
gboolean dbus_gvalue_marshal   (DBusMessageIter *iter, GValue *value);


G_END_DECLS

#endif /* DBUS_GOBJECT_VALUE_H */

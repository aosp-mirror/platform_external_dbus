#ifndef DBUS_GOBJECT_VALUE_H
#define DBUS_GOBJECT_VALUE_H

#include <dbus/dbus.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/* Used for return value storage */
typedef union
{
  gboolean gboolean_val;
  guchar guchar_val;
  gint int_val;
  gint64 gint64_val;
  guint64 guint64_val;
  double double_val;
  gpointer gpointer_val;
  char * chararray_val;
} DBusBasicGValue;

const char *   dbus_gvalue_genmarshal_name_from_type (const char *type);

const char *   dbus_gvalue_ctype_from_type           (const char *type, gboolean in);

const char *   dbus_gtype_to_dbus_type    (GType            type);

gboolean       dbus_gvalue_init           (int              type,
					   GValue          *value);

gboolean       dbus_gvalue_demarshal      (DBusMessageIter *iter,
					   GValue          *value);
gboolean       dbus_gvalue_marshal        (DBusMessageIter *iter,
					   GValue          *value);

gboolean       dbus_gvalue_store          (GValue          *value,
					   gpointer         storage);

G_END_DECLS

#endif /* DBUS_GOBJECT_VALUE_H */

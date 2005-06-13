#ifndef DBUS_GOBJECT_VALUE_H
#define DBUS_GOBJECT_VALUE_H

#include <dbus/dbus.h>
#include <dbus/dbus-signature.h>
#include <glib.h>
#include <glib-object.h>
#include "dbus/dbus-glib.h"

G_BEGIN_DECLS

typedef struct {
  DBusGConnection    *gconnection;
  DBusGProxy         *proxy;
} DBusGValueMarshalCtx;

void           dbus_g_value_types_init        (void);

GType          dbus_gtype_from_signature      (const char              *signature,
					       gboolean                 is_client);

GType          dbus_gtype_from_signature_iter (DBusSignatureIter       *sigiter,
					       gboolean                 is_client);

const char *   dbus_gtype_to_signature        (GType                    type);

GArray *       dbus_gtypes_from_arg_signature (const char              *signature,
					       gboolean                 is_client);

gboolean       dbus_gvalue_demarshal          (DBusGValueMarshalCtx    *context,
					       DBusMessageIter         *iter,
					       GValue                  *value,
					       GError                 **error);

gboolean       dbus_gvalue_demarshal_variant  (DBusGValueMarshalCtx    *context,
					       DBusMessageIter         *iter,
					       GValue                  *value,
					       GError                 **error);

GValueArray *  dbus_gvalue_demarshal_message  (DBusGValueMarshalCtx    *context,
					       DBusMessage             *message,
					       guint                    n_params,
					       const GType             *types, 
					       GError                 **error);

gboolean       dbus_gvalue_marshal            (DBusMessageIter         *iter,
					       GValue                  *value);

G_END_DECLS

#endif /* DBUS_GOBJECT_VALUE_H */

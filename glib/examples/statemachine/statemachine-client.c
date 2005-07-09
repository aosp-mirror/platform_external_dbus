#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "sm-marshal.h"

static void lose (const char *fmt, ...) G_GNUC_NORETURN G_GNUC_PRINTF (1, 2);
static void lose_gerror (const char *prefix, GError *error) G_GNUC_NORETURN;

static void
lose (const char *str, ...)
{
  va_list args;
  GtkWidget *dialog;
  char *text;

  va_start (args, str);

  text = g_strdup_vprintf (str, args);

  va_end (args);

  dialog = gtk_message_dialog_new (NULL,
				   GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_ERROR,
				   GTK_BUTTONS_CLOSE,
				   "%s",
				   text);
  gtk_dialog_run (GTK_DIALOG (dialog));

  g_free (text);

  exit (1);
}

static void
lose_gerror (const char *prefix, GError *error) 
{
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (NULL,
				   GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_ERROR,
				   GTK_BUTTONS_CLOSE,
				   "%s",
				   prefix);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
					    "%s",
					    error->message);

  gtk_dialog_run (GTK_DIALOG (dialog));

  exit (1);
}

typedef struct
{
  char *name;
  char *state;
  gdouble progress;
  DBusGProxy *proxy;
  DBusGProxyCall *get_progress_call;
} MachineInfo;

typedef struct
{
  GtkWindow *window;
  GtkWidget *view;
  GtkTreeModel *store;

  DBusGConnection *bus;
  DBusGProxy *server_proxy;

  GSList *pending_creation_calls;
  DBusGProxyCall *get_machines_call;
} ClientState;

static gboolean
proxy_to_iter (GtkTreeModel *model, DBusGProxy *proxy, GtkTreeIter *iter)
{
  if (!gtk_tree_model_get_iter_first (model, iter))
    return FALSE;
  do {
    MachineInfo *info;
    gtk_tree_model_get (model, iter, 0, &info, -1);
    if (info->proxy == proxy)
      return TRUE;
  } while (gtk_tree_model_iter_next (model, iter));
  return FALSE;
}

static void
signal_row_change (ClientState *state, GtkTreeIter *iter)
{
  GtkTreePath *path;
  path = gtk_tree_model_get_path (state->store, iter);
  gtk_tree_model_row_changed (state->store, path, iter);
  gtk_tree_path_free (path);
}

static void
get_machine_info_cb (DBusGProxy *proxy,
		     DBusGProxyCall *call,
		     gpointer data)
{
  GtkTreeIter iter;
  ClientState *state = data;
  GError *error = NULL;
  char *name, *statename;
  MachineInfo *info;

  if (!dbus_g_proxy_end_call (proxy, call, &error,
			      G_TYPE_STRING, &name,
			      G_TYPE_STRING, &statename,
			      G_TYPE_INVALID))
    lose_gerror ("Couldn't complete GetInfo", error);

  if (!proxy_to_iter (state->store, proxy, &iter))
    g_assert_not_reached ();
  
  gtk_tree_model_get (state->store, &iter, 0, &info, -1);
  g_free (info->name);
  info->name = name;
  g_free (info->state);
  info->state = statename;
  signal_row_change (state, &iter);
}

static void
set_proxy_acquisition_progress (ClientState *state,
				DBusGProxy *proxy,
				double progress)
{
  MachineInfo *info;
  GtkTreeIter iter;

  if (!proxy_to_iter (state->store, proxy, &iter))
    g_assert_not_reached ();
  gtk_tree_model_get (state->store, &iter, 0, &info, -1);

  /* Ignore machines in unknown state */
  if (!info->state)
    return;
  
  if (strcmp (info->state, "Acquired"))
    lose ("Got AcquisitionProgress signal in bad state %s",
	  info->state);

  g_print ("Got acquisition progress change for %p (%s) to %f\n", proxy, info->name ? info->name : "(unknown)", progress);

  info->progress = progress;

  signal_row_change (state, &iter);
}

static void
proxy_acquisition_changed_cb (DBusGProxy *proxy,
			      double progress,
			      gpointer user_data)
{
  set_proxy_acquisition_progress (user_data, proxy, progress);
}

static void
get_acquiring_progress_cb (DBusGProxy *proxy,
			   DBusGProxyCall *call,
			   gpointer user_data)
{
  GError *error = NULL;
  MachineInfo *info;
  GtkTreeIter iter;
  ClientState *state = user_data;
  gdouble progress;

  if (!proxy_to_iter (state->store, proxy, &iter))
    g_assert_not_reached ();
  gtk_tree_model_get (state->store, &iter, 0, &info, -1);

  g_assert (info->get_progress_call == call);

  if (!dbus_g_proxy_end_call (proxy, call, &error,
			      G_TYPE_DOUBLE, &progress, G_TYPE_INVALID))
    lose_gerror ("Failed to complete GetAcquiringProgress call", error);
  info->get_progress_call = NULL;

  set_proxy_acquisition_progress (state, proxy, progress);
}

static void
proxy_state_changed_cb (DBusGProxy *proxy,
			const char *statename,
			gpointer user_data)
{
  MachineInfo *info;
  GtkTreeIter iter;
  ClientState *state = user_data;

  if (!proxy_to_iter (state->store, proxy, &iter))
    g_assert_not_reached ();
  gtk_tree_model_get (state->store, &iter, 0, &info, -1);

  g_print ("Got state change for %p (%s) to %s\n", proxy, info->name ? info->name : "(unknown)", statename);

  g_free (info->state);
  info->state = g_strdup (statename);

  if (!strcmp (info->state, "Acquired"))
    {
      g_print ("Starting GetAcquiringProgress call for %p\n", info->proxy);
      if (info->get_progress_call != NULL)
	{
	  dbus_g_proxy_cancel_call (info->proxy, info->get_progress_call);
	  info->get_progress_call = NULL;
	}
      info->get_progress_call = 
	dbus_g_proxy_begin_call (info->proxy, "GetAcquiringProgress",
				 get_acquiring_progress_cb,
				 state, NULL,
				 G_TYPE_INVALID);
    }
  else
    info->progress = 0.0;

  signal_row_change (state, &iter);
}

static void
add_machine (ClientState *state,
	     const char *name,
	     const char *mstate,	     
	     const char *path)
{
  MachineInfo *info;
  GtkTreeIter iter;

  info = g_new0 (MachineInfo, 1);
  info->name = g_strdup (name);
  info->state = g_strdup (mstate);
  info->progress = 0.0;

  info->proxy = dbus_g_proxy_new_for_name (state->bus,
					   "com.example.StateServer",
					   path,
					   "com.example.StateMachine");

  if (!info->state)
    {
      g_print ("Starting GetInfo call for %p\n", info->proxy);
      dbus_g_proxy_begin_call (info->proxy, "GetInfo",
			       get_machine_info_cb,
			       state, NULL,
			       G_TYPE_INVALID);
    }

  /* Watch for state changes */
  dbus_g_proxy_add_signal (info->proxy, "StateChanged",
			   G_TYPE_STRING, G_TYPE_INVALID);
  
  dbus_g_proxy_connect_signal (info->proxy,
			       "StateChanged", 
			       G_CALLBACK (proxy_state_changed_cb),
			       state,
			       NULL);

  dbus_g_proxy_add_signal (info->proxy, "AcquisitionProgress",
			   G_TYPE_DOUBLE, G_TYPE_INVALID);
  
  dbus_g_proxy_connect_signal (info->proxy,
			       "AcquisitionProgress", 
			       G_CALLBACK (proxy_acquisition_changed_cb),
			       state,
			       NULL);

  gtk_list_store_prepend (GTK_LIST_STORE (state->store), &iter);
  gtk_list_store_set (GTK_LIST_STORE (state->store), &iter, 0, info, -1);

}

static void
machine_created_cb (DBusGProxy *proxy,
		    const char *name,
		    const char *path,
		    gpointer data)
{
  ClientState *state = data;
  
  add_machine (state, name, NULL, path);
}

static void
server_destroyed_cb (DBusGProxy *proxy, gpointer data)
{
  g_print ("Server terminated!\n");
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (NULL,
				   GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_INFO,
				   GTK_BUTTONS_CLOSE,
				   "State Machine server has exited");

  gtk_dialog_run (GTK_DIALOG (dialog));

  exit (1);
}

static void
window_destroyed_cb (GtkWidget *window, gpointer data)
{
  gtk_main_quit ();
}

static void
create_machine_completed_cb (DBusGProxy *proxy, DBusGProxyCall *call, gpointer data)
{
  GError *error = NULL;
  ClientState *state = data;

  if (!dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID))
    {
      /* Ignore NameInUse errors */
      if (dbus_g_error_has_name (error, "com.example.StateServer.NameInUse"))
	;
      else
	lose_gerror ("Failed to create new state machine", error);
    }
  g_print ("machine created successfully\n");
  state->pending_creation_calls = g_slist_remove (state->pending_creation_calls, call);
}

static void
send_create_machine (ClientState *state)
{
  DBusGProxyCall *call;
  char *name;
  gint n_children;

  n_children = gtk_tree_model_iter_n_children (state->store, NULL);
  name = g_strdup_printf ("machine%d", n_children);
	
  g_print ("Invoking CreateMachine(%s)\n", name);
  call = dbus_g_proxy_begin_call (state->server_proxy, "CreateMachine",
				  create_machine_completed_cb,
				  state, NULL,
				  G_TYPE_STRING, name, G_TYPE_INVALID);
  g_free (name);
  state->pending_creation_calls = g_slist_prepend (state->pending_creation_calls, call);
}

static void
do_a_state_change (ClientState *state)
{
  gint index;
  GtkTreeIter iter;
  gint n_children;
  MachineInfo *info;

  n_children = gtk_tree_model_iter_n_children (state->store, NULL);
  if (n_children == 0)
    {
      g_print ("No machines yet, not doing a state switch\n");
      return;
    }

  index = g_random_int_range (0, n_children);
  gtk_tree_model_iter_nth_child (state->store, &iter, NULL, index);
  gtk_tree_model_get (state->store, &iter, 0, &info, -1);

  if (!info->state)
    {
      g_print ("Machine not yet in known state, skipping state switch\n");
      return;
    }

  if (!strcmp (info->state, "Shutdown"))
    {
      g_print ("Sending Start request to machine %s\n", info->name);
      dbus_g_proxy_call_no_reply (info->proxy, "Start", G_TYPE_INVALID);
    }
  else if (!strcmp (info->state, "Loading"))
    {
      
      g_print ("Sending Reacquire request to machine %s\n", info->name);
      dbus_g_proxy_call_no_reply (info->proxy, "Reacquire", G_TYPE_INVALID);
    }
  else
    {
      g_print ("Sending Shutdown request to machine %s\n", info->name);
      dbus_g_proxy_call_no_reply (info->proxy, "Shutdown", G_TYPE_INVALID);
    }
}

static gboolean
do_something_random_2 (gpointer data)
{
  ClientState *state = data;
  do_a_state_change (state);
  g_timeout_add (g_random_int_range (2000, 5000), do_something_random_2, state);
  return FALSE;
}

static gboolean
do_something_random (gpointer data)
{
  ClientState *state = data;
  gint n_children;

  switch (g_random_int_range (0, 3))
    {
    case 0:
      send_create_machine (state);
      break;
    case 1:
    case 2:
      do_a_state_change (state);
      break;
    default:
      g_assert_not_reached ();
    }
      
  n_children = gtk_tree_model_iter_n_children (state->store, NULL);
  if (n_children >= 5)
    {
      g_print ("MAX children reached, switching to state changes only\n");
      g_timeout_add (g_random_int_range (500, 3000), do_something_random_2, state);
    }
  else
    g_timeout_add (g_random_int_range (500, 3000), do_something_random, state);
  return FALSE;
}

static void 
set_cell_name (GtkTreeViewColumn *tree_column,
	       GtkCellRenderer   *cell,
	       GtkTreeModel      *tree_model,
	       GtkTreeIter       *iter,
	       gpointer           data)
{
  MachineInfo *info;
  
  gtk_tree_model_get (tree_model, iter, 0, &info, -1);
  
  g_object_set (cell, "text", info->name ? info->name : "", NULL);
}

static gint
sort_by_name (GtkTreeModel *model,
	      GtkTreeIter  *a,
	      GtkTreeIter  *b,
	      gpointer      user_data)
{
  MachineInfo *info_a, *info_b;

  gtk_tree_model_get (model, a, 0, &info_a, -1);
  gtk_tree_model_get (model, b, 0, &info_b, -1);

  return strcmp (info_a->name ? info_a->name : "", 
		 info_b ? info_b->name : "");
}

static void 
set_cell_state (GtkTreeViewColumn *tree_column,
	       GtkCellRenderer   *cell,
	       GtkTreeModel      *tree_model,
	       GtkTreeIter       *iter,
	       gpointer           data)
{
  MachineInfo *info;
  
  gtk_tree_model_get (tree_model, iter, 0, &info, -1);
  
  g_object_set (cell, "text", info->state ? info->state : "", NULL);
}

static gint
sort_by_state (GtkTreeModel *model,
	       GtkTreeIter  *a,
	       GtkTreeIter  *b,
	       gpointer      user_data)
{
  MachineInfo *info_a, *info_b;

  gtk_tree_model_get (model, a, 0, &info_a, -1);
  gtk_tree_model_get (model, b, 0, &info_b, -1);

  return strcmp (info_a->state ? info_a->state : "", 
		 info_b ? info_b->state : "");
}

static void 
set_cell_progress (GtkTreeViewColumn *tree_column,
		   GtkCellRenderer   *cell,
		   GtkTreeModel      *tree_model,
		   GtkTreeIter       *iter,
		   gpointer           data)
{
  MachineInfo *info;
  
  gtk_tree_model_get (tree_model, iter, 0, &info, -1);
  
  g_object_set (cell, "value", (int) (info->progress * 100), NULL);
}

static gint
sort_by_progress (GtkTreeModel *model,
		  GtkTreeIter  *a,
		  GtkTreeIter  *b,
		  gpointer      user_data)
{
  MachineInfo *info_a, *info_b;

  gtk_tree_model_get (model, a, 0, &info_a, -1);
  gtk_tree_model_get (model, b, 0, &info_b, -1);

  return info_a->progress > info_b->progress ? 1 : -1;
}

static void
get_machines_cb (DBusGProxy *proxy, DBusGProxyCall *call, gpointer data)
{
  GError *error = NULL;
  ClientState *state = data;
  GPtrArray *objs;
  guint i;
  GtkWidget *scrolledwin;
  GtkTreeViewColumn *col;
  GtkCellRenderer *rend;

  g_assert (call == state->get_machines_call);

  if (!dbus_g_proxy_end_call (proxy, call, &error,
			      dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH),
			      &objs,
			      G_TYPE_INVALID))
    lose_gerror ("Failed to get current machine list", error);

  gtk_container_remove (GTK_CONTAINER (state->window),
			gtk_bin_get_child (GTK_BIN (state->window)));

  scrolledwin = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_show (scrolledwin);

  state->store = GTK_TREE_MODEL (gtk_list_store_new (1, G_TYPE_POINTER));
  state->view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (state->store));  
  gtk_widget_show (state->view);
  gtk_container_add (GTK_CONTAINER (scrolledwin), state->view);
  gtk_container_add (GTK_CONTAINER (state->window), scrolledwin);

  rend = gtk_cell_renderer_text_new ();
  col = gtk_tree_view_column_new_with_attributes (_("Name"), 
						  rend, 
						  NULL);
  gtk_tree_view_column_set_cell_data_func (col, rend, set_cell_name, NULL, NULL);
  gtk_tree_view_column_set_resizable (col, TRUE);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (state->store), 
				   0, sort_by_name, NULL, NULL);
  gtk_tree_view_column_set_sort_column_id (col, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW (state->view), col);

  rend = gtk_cell_renderer_text_new ();
  col = gtk_tree_view_column_new_with_attributes (_("State"), 
						  rend, 
						  NULL);
  gtk_tree_view_column_set_cell_data_func (col, rend, set_cell_state, NULL, NULL);
  gtk_tree_view_column_set_resizable (col, TRUE);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (state->store), 
				   0, sort_by_state, NULL, NULL);
  gtk_tree_view_column_set_sort_column_id (col, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW (state->view), col);

  rend = gtk_cell_renderer_progress_new ();
  col = gtk_tree_view_column_new_with_attributes (_("Progress"), 
						  rend, 
						  NULL);
  gtk_tree_view_column_set_cell_data_func (col, rend, set_cell_progress, NULL, NULL);
  gtk_tree_view_column_set_resizable (col, TRUE);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (state->store), 
				   0, sort_by_progress, NULL, NULL);
  gtk_tree_view_column_set_sort_column_id (col, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW (state->view), col);
  
  for (i = 0; i < objs->len; i++)
    {
      add_machine (state, NULL, NULL, g_ptr_array_index (objs, i));
      g_free (g_ptr_array_index (objs, i));
    }
  g_ptr_array_free (objs, TRUE);

  g_idle_add (do_something_random, state);
}

int
main (int argc, char **argv)
{
  DBusGConnection *bus;
  DBusGProxy *server;
  GError *error = NULL;
  ClientState state;
  GtkWidget *label;

  gtk_init (&argc, &argv);

  g_log_set_always_fatal (G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);

  state.window = GTK_WINDOW (gtk_window_new (GTK_WINDOW_TOPLEVEL));
  gtk_window_set_resizable (GTK_WINDOW (state.window), TRUE);
  g_signal_connect (G_OBJECT (state.window), "destroy",
		    G_CALLBACK (window_destroyed_cb),
		    &state);
  gtk_window_set_title (GTK_WINDOW (state.window), _("D-BUS State Machine Demo"));
  gtk_window_set_default_size (GTK_WINDOW (state.window), 320, 240);

  label = gtk_label_new ("");
  gtk_label_set_markup (GTK_LABEL (label), "<b>Loading...</b>");
  gtk_widget_show (label);

  gtk_container_add (GTK_CONTAINER (state.window), label); 

  bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
  if (!bus)
    lose_gerror ("Couldn't connect to session bus", error);

  state.bus = bus;

  server = dbus_g_proxy_new_for_name_owner (bus,
					    "com.example.StateServer",
					    "/com/example/StateServer",
					    "com.example.StateMachineServer",
					    &error);
  if (!server)
    lose_gerror ("Couldn't find \"com.example.StateServer\"", error);

  state.server_proxy = server;

  g_signal_connect (server, "destroy",
		    G_CALLBACK (server_destroyed_cb),
		    &state);

  dbus_g_object_register_marshaller (sm_marshal_VOID__STRING_BOXED,
				     G_TYPE_NONE, G_TYPE_STRING,
				     DBUS_TYPE_G_OBJECT_PATH);

  dbus_g_proxy_add_signal (server, "MachineCreated", G_TYPE_STRING, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);

  dbus_g_proxy_connect_signal (server, "MachineCreated",
			       G_CALLBACK (machine_created_cb),
			       &state, NULL);

  state.get_machines_call = dbus_g_proxy_begin_call (server, "GetMachines",
						     get_machines_cb, &state, NULL,
						     G_TYPE_INVALID);
  
  gtk_widget_show (GTK_WIDGET (state.window));
  
  gtk_main ();

  g_object_unref (G_OBJECT (server));

  exit(0);
}

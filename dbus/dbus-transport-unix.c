/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-transport-unix.c UNIX socket subclasses of DBusTransport
 *
 * Copyright (C) 2002  Red Hat Inc.
 *
 * Licensed under the Academic Free License version 1.2
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "dbus-internals.h"
#include "dbus-connection-internal.h"
#include "dbus-transport-unix.h"
#include "dbus-transport-protected.h"
#include "dbus-watch.h"
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>


/**
 * @defgroup DBusTransportUnix DBusTransport implementations for UNIX
 * @ingroup  DBusInternals
 * @brief Implementation details of DBusTransport on UNIX
 *
 * @{
 */

/**
 * Opaque object representing a Unix file descriptor transport.
 */
typedef struct DBusTransportUnix DBusTransportUnix;

/**
 * Implementation details of DBusTransportUnix. All members are private.
 */
struct DBusTransportUnix
{
  DBusTransport base;                   /**< Parent instance */
  int fd;                               /**< File descriptor. */
  DBusWatch *watch;                     /**< Watch for readability. */
  DBusWatch *write_watch;               /**< Watch for writability. */

  int max_bytes_read_per_iteration;     /**< To avoid blocking too long. */
  int max_bytes_written_per_iteration;  /**< To avoid blocking too long. */

  int message_bytes_written;            /**< Number of bytes of current
                                         *   outgoing message that have
                                         *   been written.
                                         */
};

static void
unix_finalize (DBusTransport *transport)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  
  _dbus_transport_finalize_base (transport);

  if (unix_transport->watch)
    {
      _dbus_watch_invalidate (unix_transport->watch);
      _dbus_watch_unref (unix_transport->watch);
    }
  
  dbus_free (transport);
}

static void
do_io_error (DBusTransport *transport)
{
  _dbus_transport_disconnect (transport);
  _dbus_connection_transport_error (transport->connection,
                                    DBUS_RESULT_DISCONNECTED);
}

static void
do_writing (DBusTransport *transport)
{
  int total;
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  
  total = 0;

  while (_dbus_connection_have_messages_to_send (transport->connection))
    {
      int bytes_written;
      DBusMessage *message;
      const DBusString *header;
      const DBusString *body;
      int header_len, body_len;
      
      if (total > unix_transport->max_bytes_written_per_iteration)
        {
          _dbus_verbose ("%d bytes exceeds %d bytes written per iteration, returning\n",
                         total, unix_transport->max_bytes_written_per_iteration);
          goto out;
        }
      
      message = _dbus_connection_get_message_to_send (transport->connection);
      _dbus_assert (message != NULL);
      _dbus_message_lock (message);

      _dbus_message_get_network_data (message,
                                      &header, &body);

      header_len = _dbus_string_get_length (header);
      body_len = _dbus_string_get_length (body);
      
      if (unix_transport->message_bytes_written < header_len)
        {
          bytes_written =
            _dbus_write_two (unix_transport->fd,
                             header,
                             unix_transport->message_bytes_written,
                             header_len - unix_transport->message_bytes_written,
                             body,
                             0, body_len);
        }
      else
        {
          bytes_written =
            _dbus_write (unix_transport->fd,
                         body,
                         (unix_transport->message_bytes_written - header_len),
                         body_len -
                         (unix_transport->message_bytes_written - header_len));
        }

      if (bytes_written < 0)
        {
          /* EINTR already handled for us */
          
          if (errno == EAGAIN ||
              errno == EWOULDBLOCK)
            goto out;
          else
            {
              _dbus_verbose ("Error writing to message bus: %s\n",
                             _dbus_strerror (errno));
              do_io_error (transport);
              goto out;
            }
        }
      else
        {          
          _dbus_verbose (" wrote %d bytes\n", bytes_written);
          
          total += bytes_written;
          unix_transport->message_bytes_written += bytes_written;

          _dbus_assert (unix_transport->message_bytes_written <=
                        (header_len + body_len));
          
          if (unix_transport->message_bytes_written == (header_len + body_len))
            {
              _dbus_connection_message_sent (transport->connection,
                                             message);
              unix_transport->message_bytes_written = 0;
            }
        }
    }

 out:
  return; /* I think some C compilers require a statement after a label */
}

static void
do_reading (DBusTransport *transport)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  DBusString *buffer;
  int buffer_len;
  int bytes_read;
  int total;
  
  total = 0;

 again:
  
  if (total > unix_transport->max_bytes_read_per_iteration)
    {
      _dbus_verbose ("%d bytes exceeds %d bytes read per iteration, returning\n",
                     total, unix_transport->max_bytes_read_per_iteration);
      goto out;
    }

  _dbus_message_loader_get_buffer (transport->loader,
                                   &buffer);

  buffer_len = _dbus_string_get_length (buffer);  
  
  bytes_read = _dbus_read (unix_transport->fd,
                           buffer, unix_transport->max_bytes_read_per_iteration);

  _dbus_message_loader_return_buffer (transport->loader,
                                      buffer,
                                      bytes_read < 0 ? 0 : bytes_read);
  
  if (bytes_read < 0)
    {
      /* EINTR already handled for us */
      
      if (errno == EAGAIN ||
          errno == EWOULDBLOCK)
        goto out;
      else
        {
          _dbus_verbose ("Error reading from message bus: %s\n",
                         _dbus_strerror (errno));
          do_io_error (transport);
          goto out;
        }
    }
  else if (bytes_read == 0)
    {
      _dbus_verbose ("Disconnected from message bus\n");
      do_io_error (transport);
      goto out;
    }
  else
    {
      DBusMessage *message;
      
      _dbus_verbose (" read %d bytes\n", bytes_read);
      
      total += bytes_read;      

      /* Queue any messages */
      while ((message = _dbus_message_loader_pop_message (transport->loader)))
        {
          _dbus_verbose ("queueing received message %p\n", message);
          
          _dbus_connection_queue_received_message (transport->connection,
                                                   message);
          dbus_message_unref (message);
        }
      
      /* Try reading more data until we get EAGAIN and return, or
       * exceed max bytes per iteration.  If in blocking mode of
       * course we'll block instead of returning.
       */
      goto again;
    }

 out:
  return; /* I think some C compilers require a statement after a label */
}

static void
unix_handle_watch (DBusTransport *transport,
                   DBusWatch     *watch,
                   unsigned int   flags)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;

  _dbus_assert (watch == unix_transport->watch ||
                watch == unix_transport->write_watch);

  if (flags & (DBUS_WATCH_HANGUP | DBUS_WATCH_ERROR))
    {
      _dbus_transport_disconnect (transport);
      _dbus_connection_transport_error (transport->connection,
                                        DBUS_RESULT_DISCONNECTED);
      return;
    }
  
  if (watch == unix_transport->watch &&
      (flags & DBUS_WATCH_READABLE))
    do_reading (transport);
  else if (watch == unix_transport->write_watch &&
           (flags & DBUS_WATCH_WRITABLE))
    do_writing (transport); 
}

static void
unix_disconnect (DBusTransport *transport)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;

  if (unix_transport->watch)
    {
      _dbus_connection_remove_watch (transport->connection,
                                     unix_transport->watch);
      _dbus_watch_invalidate (unix_transport->watch);
      _dbus_watch_unref (unix_transport->watch);
      unix_transport->watch = NULL;
    }
  
  close (unix_transport->fd);
  unix_transport->fd = -1;
}

static void
unix_connection_set (DBusTransport *transport)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  DBusWatch *watch;

  _dbus_assert (unix_transport->watch == NULL);
  
  watch = _dbus_watch_new (unix_transport->fd,
                           DBUS_WATCH_READABLE);
  
  if (watch == NULL)
    {
      _dbus_transport_disconnect (transport);
      return;
    }
  
  if (!_dbus_connection_add_watch (transport->connection,
                                   watch))
    {
      _dbus_transport_disconnect (transport);
      return;
    }

  unix_transport->watch = watch;
}

static void
unix_messages_pending (DBusTransport *transport,
                       int            messages_pending)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;

  if (messages_pending > 0 &&
      unix_transport->write_watch == NULL)
    {
      unix_transport->write_watch =
        _dbus_watch_new (unix_transport->fd,
                         DBUS_WATCH_WRITABLE);

      /* we can maybe add it some other time, just silently bomb */
      if (unix_transport->write_watch == NULL)
        return;

      if (!_dbus_connection_add_watch (transport->connection,
                                       unix_transport->write_watch))
        {
          _dbus_watch_invalidate (unix_transport->write_watch);
          _dbus_watch_unref (unix_transport->write_watch);
          unix_transport->write_watch = NULL;
        }
    }
  else if (messages_pending == 0 &&
           unix_transport->write_watch != NULL)
    {
      _dbus_connection_remove_watch (transport->connection,
                                     unix_transport->write_watch);
      _dbus_watch_invalidate (unix_transport->write_watch);
      _dbus_watch_unref (unix_transport->write_watch);
      unix_transport->write_watch = NULL;
    }
}

static  void
unix_do_iteration (DBusTransport *transport,
                   unsigned int   flags,
                   int            timeout_milliseconds)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  fd_set read_set;
  fd_set write_set;
  dbus_bool_t do_select;
  
  do_select = FALSE;
  
  FD_ZERO (&read_set);
  if (flags & DBUS_ITERATION_DO_READING)
    {
      FD_SET (unix_transport->fd, &read_set);
      do_select = TRUE;
    }
  
  FD_ZERO (&write_set);
  if (flags & DBUS_ITERATION_DO_WRITING)
    {
      FD_SET (unix_transport->fd, &write_set);
      do_select = TRUE;
    }

  if (do_select)
    {
      fd_set err_set;
      struct timeval timeout;
      dbus_bool_t use_timeout;

    again:
      
      FD_ZERO (&err_set);
      FD_SET (unix_transport->fd, &err_set);
  
      if (flags & DBUS_ITERATION_BLOCK)
        {
          if (timeout_milliseconds >= 0)
            {
              timeout.tv_sec = timeout_milliseconds / 1000;
              timeout.tv_usec = (timeout_milliseconds % 1000) * 1000;
              
              /* Always use timeout if one is passed in. */
              use_timeout = TRUE;
            }
          else
            {
              use_timeout = FALSE; /* NULL timeout to block forever */
            }
        }
      else
        {
          /* 0 timeout to not block */
          timeout.tv_sec = 0;
          timeout.tv_usec = 0;
          use_timeout = TRUE;
        }
      
      if (select (unix_transport->fd + 1, &read_set, &write_set, &err_set,
                  use_timeout ? &timeout : NULL) >= 0)
        {
          if (FD_ISSET (unix_transport->fd, &err_set))
            do_io_error (transport);
          else
            {
              if (FD_ISSET (unix_transport->fd, &read_set))
                do_reading (transport);
              if (FD_ISSET (unix_transport->fd, &write_set))
                do_writing (transport);
            }
        }
      else if (errno == EINTR)
        goto again;
      else
        {
          _dbus_verbose ("Error from select(): %s\n",
                         _dbus_strerror (errno));
        }
    }
}

static DBusTransportVTable unix_vtable = {
  unix_finalize,
  unix_handle_watch,
  unix_disconnect,
  unix_connection_set,
  unix_messages_pending,
  unix_do_iteration
};

/**
 * Creates a new transport for the given file descriptor.  The file
 * descriptor must be nonblocking (use _dbus_set_fd_nonblocking() to
 * make it so). This function is shared by various transports that
 * boil down to a full duplex file descriptor.
 *
 * @param fd the file descriptor.
 * @returns the new transport, or #NULL if no memory.
 */
DBusTransport*
_dbus_transport_new_for_fd (int fd)
{
  DBusTransportUnix *unix_transport;
  
  unix_transport = dbus_new0 (DBusTransportUnix, 1);
  if (unix_transport == NULL)
    return NULL;

  if (!_dbus_transport_init_base (&unix_transport->base,
                                  &unix_vtable))
    {
      dbus_free (unix_transport);
      return NULL;
    }
  
  unix_transport->fd = fd;
  unix_transport->message_bytes_written = 0;
  
  /* These values should probably be tunable or something. */     
  unix_transport->max_bytes_read_per_iteration = 2048;
  unix_transport->max_bytes_written_per_iteration = 2048;
  
  return (DBusTransport*) unix_transport;
}

/**
 * Creates a new transport for the given Unix domain socket
 * path.
 *
 * @param path the path to the domain socket.
 * @param result location to store reason for failure.
 * @returns a new transport, or #NULL on failure.
 */
DBusTransport*
_dbus_transport_new_for_domain_socket (const char     *path,
                                       DBusResultCode *result)
{
  int fd;
  DBusTransport *transport;

  fd = _dbus_connect_unix_socket (path, result);
  if (fd < 0)
    return NULL;
  
  transport = _dbus_transport_new_for_fd (fd);
  if (transport == NULL)
    {
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      close (fd);
      fd = -1;
    }

  return transport;
}


/** @} */


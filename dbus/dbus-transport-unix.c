/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-transport-unix.c UNIX socket subclasses of DBusTransport
 *
 * Copyright (C) 2002, 2003  Red Hat Inc.
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
  DBusWatch *read_watch;                /**< Watch for readability. */
  DBusWatch *write_watch;               /**< Watch for writability. */

  int max_bytes_read_per_iteration;     /**< To avoid blocking too long. */
  int max_bytes_written_per_iteration;  /**< To avoid blocking too long. */

  int message_bytes_written;            /**< Number of bytes of current
                                         *   outgoing message that have
                                         *   been written.
                                         */
  DBusString encoded_message;           /**< Encoded version of current
                                         *   outgoing message.
                                         */
};

static void
free_watches (DBusTransport *transport)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  
  if (unix_transport->read_watch)
    {
      if (transport->connection)
        _dbus_connection_remove_watch (transport->connection,
                                       unix_transport->read_watch);
      _dbus_watch_invalidate (unix_transport->read_watch);
      _dbus_watch_unref (unix_transport->read_watch);
      unix_transport->read_watch = NULL;
    }

  if (unix_transport->write_watch)
    {
      if (transport->connection)
        _dbus_connection_remove_watch (transport->connection,
                                       unix_transport->write_watch);
      _dbus_watch_invalidate (unix_transport->write_watch);
      _dbus_watch_unref (unix_transport->write_watch);
      unix_transport->write_watch = NULL;
    }
}

static void
unix_finalize (DBusTransport *transport)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  
  free_watches (transport);

  _dbus_string_free (&unix_transport->encoded_message);
  
  _dbus_transport_finalize_base (transport);

  _dbus_assert (unix_transport->read_watch == NULL);
  _dbus_assert (unix_transport->write_watch == NULL);
  
  dbus_free (transport);
}

static void
check_write_watch (DBusTransport *transport)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  dbus_bool_t need_write_watch;

  if (transport->connection == NULL)
    return;

  if (transport->disconnected)
    {
      _dbus_assert (unix_transport->write_watch == NULL);
      return;
    }
  
  _dbus_transport_ref (transport);

  if (_dbus_transport_get_is_authenticated (transport))
    need_write_watch = transport->messages_need_sending;
  else
    need_write_watch = transport->send_credentials_pending ||
      _dbus_auth_do_work (transport->auth) == DBUS_AUTH_STATE_HAVE_BYTES_TO_SEND;

  _dbus_connection_toggle_watch (transport->connection,
                                 unix_transport->write_watch,
                                 need_write_watch);

  _dbus_transport_unref (transport);
}

static void
check_read_watch (DBusTransport *transport)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  dbus_bool_t need_read_watch;

  if (transport->connection == NULL)
    return;

  if (transport->disconnected)
    {
      _dbus_assert (unix_transport->read_watch == NULL);
      return;
    }
  
  _dbus_transport_ref (transport);

  if (_dbus_transport_get_is_authenticated (transport))
    need_read_watch =
      _dbus_counter_get_value (transport->live_messages_size) < transport->max_live_messages_size;
  else
    need_read_watch = transport->receive_credentials_pending ||
      _dbus_auth_do_work (transport->auth) == DBUS_AUTH_STATE_WAITING_FOR_INPUT;

  _dbus_connection_toggle_watch (transport->connection,
                                 unix_transport->read_watch,
                                 need_read_watch);

  _dbus_transport_unref (transport);
}

static void
do_io_error (DBusTransport *transport)
{
  _dbus_transport_ref (transport);
  _dbus_transport_disconnect (transport);
  _dbus_transport_unref (transport);
}

static void
queue_messages (DBusTransport *transport)
{
  DBusMessage *message;
  
  /* Queue any messages */
  while ((message = _dbus_message_loader_pop_message (transport->loader)))
    {
      _dbus_verbose ("queueing received message %p\n", message);

      _dbus_message_add_size_counter (message, transport->live_messages_size);
      if (!_dbus_connection_queue_received_message (transport->connection,
                                                    message))
        /* FIXME woops! */;
        
      dbus_message_unref (message);
    }

  if (_dbus_message_loader_get_is_corrupted (transport->loader))
    {
      _dbus_verbose ("Corrupted message stream, disconnecting\n");
      do_io_error (transport);
    }

  /* check read watch in case we've now exceeded max outstanding messages */
  check_read_watch (transport);
}

/* return value is whether we successfully read any new data. */
static dbus_bool_t
read_data_into_auth (DBusTransport *transport)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  DBusString buffer;
  int bytes_read;
  
  if (!_dbus_string_init (&buffer, _DBUS_INT_MAX))
    {
      /* just disconnect if we don't have memory
       * to do an authentication
       */
      _dbus_verbose ("No memory for authentication\n");
      do_io_error (transport);
      return FALSE;
    }
  
  bytes_read = _dbus_read (unix_transport->fd,
                           &buffer, unix_transport->max_bytes_read_per_iteration);

  if (bytes_read > 0)
    {
      _dbus_verbose (" read %d bytes in auth phase\n", bytes_read);
      
      if (_dbus_auth_bytes_received (transport->auth,
                                     &buffer))
        {
          _dbus_string_free (&buffer);
          return TRUE; /* We did read some data! woo! */
        }
      else
        {
          /* just disconnect if we don't have memory to do an
           * authentication, don't fool with trying to save the buffer
           * and who knows what.
           */
          _dbus_verbose ("No memory for authentication\n");
          do_io_error (transport);
        }
    }
  else if (bytes_read < 0)
    {
      /* EINTR already handled for us */
      
      if (errno == EAGAIN ||
          errno == EWOULDBLOCK)
        ; /* do nothing, just return FALSE below */
      else
        {
          _dbus_verbose ("Error reading from remote app: %s\n",
                         _dbus_strerror (errno));
          do_io_error (transport);
        }
    }
  else if (bytes_read == 0)
    {
      _dbus_verbose ("Disconnected from remote app\n");
      do_io_error (transport);      
    }
  
  _dbus_string_free (&buffer);
  return FALSE;
}

/* Return value is whether we successfully wrote any bytes */
static dbus_bool_t
write_data_from_auth (DBusTransport *transport)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  int bytes_written;
  const DBusString *buffer;

  if (!_dbus_auth_get_bytes_to_send (transport->auth,
                                     &buffer))
    return FALSE;
  
  bytes_written = _dbus_write (unix_transport->fd,
                               buffer,
                               0, _dbus_string_get_length (buffer));

  if (bytes_written > 0)
    {
      _dbus_auth_bytes_sent (transport->auth, bytes_written);
      return TRUE;
    }
  else if (bytes_written < 0)
    {
      /* EINTR already handled for us */
      
      if (errno == EAGAIN ||
          errno == EWOULDBLOCK)
        ;
      else
        {
          _dbus_verbose ("Error writing to remote app: %s\n",
                         _dbus_strerror (errno));
          do_io_error (transport);
        }
    }

  return FALSE;
}

static void
recover_unused_bytes (DBusTransport *transport)
{
  
  if (_dbus_auth_needs_decoding (transport->auth))
    {
      DBusString plaintext;
      DBusString encoded;
      DBusString *buffer;
      int orig_len;
      
      if (!_dbus_string_init (&plaintext, _DBUS_INT_MAX))
        goto nomem;

      if (!_dbus_string_init (&encoded, _DBUS_INT_MAX))
        {
          _dbus_string_free (&plaintext);
          goto nomem;
        }
      
      if (!_dbus_auth_get_unused_bytes (transport->auth,
                                        &encoded))
        {
          _dbus_string_free (&plaintext);
          _dbus_string_free (&encoded);
          goto nomem;
        }
      
      if (!_dbus_auth_decode_data (transport->auth,
                                   &encoded, &plaintext))
        {
          _dbus_string_free (&plaintext);
          _dbus_string_free (&encoded);
          goto nomem;
        }
      
      _dbus_message_loader_get_buffer (transport->loader,
                                       &buffer);
      
      orig_len = _dbus_string_get_length (buffer);

      if (!_dbus_string_move (&plaintext, 0, buffer,
                              orig_len))
        {
          _dbus_string_free (&plaintext);
          _dbus_string_free (&encoded);
          goto nomem;
        }
      
      _dbus_verbose (" %d unused bytes sent to message loader\n", 
                     _dbus_string_get_length (buffer) -
                     orig_len);
      
      _dbus_message_loader_return_buffer (transport->loader,
                                          buffer,
                                          _dbus_string_get_length (buffer) -
                                          orig_len);

      _dbus_string_free (&plaintext);
      _dbus_string_free (&encoded);
    }
  else
    {
      DBusString *buffer;
      int orig_len;

      _dbus_message_loader_get_buffer (transport->loader,
                                       &buffer);
                
      orig_len = _dbus_string_get_length (buffer);
                
      if (!_dbus_auth_get_unused_bytes (transport->auth,
                                        buffer))
        goto nomem;
                
      _dbus_verbose (" %d unused bytes sent to message loader\n", 
                     _dbus_string_get_length (buffer) -
                     orig_len);
      
      _dbus_message_loader_return_buffer (transport->loader,
                                          buffer,
                                          _dbus_string_get_length (buffer) -
                                          orig_len);
    }
  
  queue_messages (transport);

  return;

 nomem:
  _dbus_verbose ("Not enough memory to transfer unused bytes from auth conversation\n");
  do_io_error (transport);
}

static void
exchange_credentials (DBusTransport *transport,
                      dbus_bool_t    do_reading,
                      dbus_bool_t    do_writing)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;

  if (do_writing && transport->send_credentials_pending)
    {
      if (_dbus_send_credentials_unix_socket (unix_transport->fd,
                                              NULL))
        {
          transport->send_credentials_pending = FALSE;
        }
      else
        {
          _dbus_verbose ("Failed to write credentials\n");
          do_io_error (transport);
        }
    }
  
  if (do_reading && transport->receive_credentials_pending)
    {
      if (_dbus_read_credentials_unix_socket (unix_transport->fd,
                                               &transport->credentials,
                                               NULL))
        {
          transport->receive_credentials_pending = FALSE;
        }
      else
        {
          _dbus_verbose ("Failed to read credentials\n");
          do_io_error (transport);
        }
    }

  if (!(transport->send_credentials_pending ||
        transport->receive_credentials_pending))
    {
      _dbus_auth_set_credentials (transport->auth,
                                  &transport->credentials);
    }
}

static void
do_authentication (DBusTransport *transport,
                   dbus_bool_t    do_reading,
                   dbus_bool_t    do_writing)
{  
  _dbus_transport_ref (transport);
  
  while (!_dbus_transport_get_is_authenticated (transport) &&
         _dbus_transport_get_is_connected (transport))
    {
      exchange_credentials (transport, do_reading, do_writing);
      
      if (transport->send_credentials_pending ||
          transport->receive_credentials_pending)
        {
          _dbus_verbose ("send_credentials_pending = %d receive_credentials_pending = %d\n",
                         transport->send_credentials_pending,
                         transport->receive_credentials_pending);
          goto out;
        }
      
      switch (_dbus_auth_do_work (transport->auth))
        {
        case DBUS_AUTH_STATE_WAITING_FOR_INPUT:
          _dbus_verbose (" auth state: waiting for input\n");
          if (!do_reading || !read_data_into_auth (transport))
            goto out;
          break;
      
        case DBUS_AUTH_STATE_WAITING_FOR_MEMORY:
          /* Screw it, just disconnect */
          _dbus_verbose (" auth state: waiting for memory\n");
          do_io_error (transport);
          break;
      
        case DBUS_AUTH_STATE_HAVE_BYTES_TO_SEND:
          _dbus_verbose (" auth state: bytes to send\n");
          if (!do_writing || !write_data_from_auth (transport))
            goto out;
          break;
      
        case DBUS_AUTH_STATE_NEED_DISCONNECT:
          _dbus_verbose (" auth state: need to disconnect\n");
          do_io_error (transport);
          break;
      
        case DBUS_AUTH_STATE_AUTHENTICATED_WITH_UNUSED_BYTES:
          _dbus_verbose (" auth state: auth with unused bytes\n");
          recover_unused_bytes (transport);
          break;
          
        case DBUS_AUTH_STATE_AUTHENTICATED:
          _dbus_verbose (" auth state: authenticated\n");
          break;
        }
    }

 out:
  check_read_watch (transport);
  check_write_watch (transport);
  _dbus_transport_unref (transport);
}

static void
do_writing (DBusTransport *transport)
{
  int total;
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;

  /* No messages without authentication! */
  if (!_dbus_transport_get_is_authenticated (transport))
    return;

  if (transport->disconnected)
    return;
  
  total = 0;

  while (!transport->disconnected &&
         _dbus_connection_have_messages_to_send (transport->connection))
    {
      int bytes_written;
      DBusMessage *message;
      const DBusString *header;
      const DBusString *body;
      int header_len, body_len;
      int total_bytes_to_write;
      
      if (total > unix_transport->max_bytes_written_per_iteration)
        {
          _dbus_verbose ("%d bytes exceeds %d bytes written per iteration, returning\n",
                         total, unix_transport->max_bytes_written_per_iteration);
          goto out;
        }

      if (unix_transport->write_watch == NULL)
        {
          _dbus_verbose ("write watch removed, not writing more stuff\n");
          goto out;
        }
      
      message = _dbus_connection_get_message_to_send (transport->connection);
      _dbus_assert (message != NULL);
      _dbus_message_lock (message);

#if 0
      _dbus_verbose ("writing message %p\n", message);
#endif
      
      _dbus_message_get_network_data (message,
                                      &header, &body);

      header_len = _dbus_string_get_length (header);
      body_len = _dbus_string_get_length (body);

      if (_dbus_auth_needs_encoding (transport->auth))
        {
          if (_dbus_string_get_length (&unix_transport->encoded_message) == 0)
            {
              if (!_dbus_auth_encode_data (transport->auth,
                                           header, &unix_transport->encoded_message))
                goto out;
              
              if (!_dbus_auth_encode_data (transport->auth,
                                           body, &unix_transport->encoded_message))
                {
                  _dbus_string_set_length (&unix_transport->encoded_message, 0);
                  goto out;
                }
            }
          
          total_bytes_to_write = _dbus_string_get_length (&unix_transport->encoded_message);

#if 0
          _dbus_verbose ("encoded message is %d bytes\n",
                         total_bytes_to_write);
#endif
          
          bytes_written =
            _dbus_write (unix_transport->fd,
                         &unix_transport->encoded_message,
                         unix_transport->message_bytes_written,
                         total_bytes_to_write - unix_transport->message_bytes_written);
        }
      else
        {
          total_bytes_to_write = header_len + body_len;

#if 0
          _dbus_verbose ("message is %d bytes\n",
                         total_bytes_to_write);          
#endif
          
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
        }

      if (bytes_written < 0)
        {
          /* EINTR already handled for us */
          
          if (errno == EAGAIN ||
              errno == EWOULDBLOCK)
            goto out;
          else
            {
              _dbus_verbose ("Error writing to remote app: %s\n",
                             _dbus_strerror (errno));
              do_io_error (transport);
              goto out;
            }
        }
      else
        {          
          _dbus_verbose (" wrote %d bytes of %d\n", bytes_written,
                         total_bytes_to_write);
          
          total += bytes_written;
          unix_transport->message_bytes_written += bytes_written;

          _dbus_assert (unix_transport->message_bytes_written <=
                        total_bytes_to_write);
          
          if (unix_transport->message_bytes_written == total_bytes_to_write)
            {
              unix_transport->message_bytes_written = 0;
              _dbus_string_set_length (&unix_transport->encoded_message, 0);

              _dbus_connection_message_sent (transport->connection,
                                             message);
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
  int bytes_read;
  int total;

  /* No messages without authentication! */
  if (!_dbus_transport_get_is_authenticated (transport))
    return;
  
  total = 0;

 again:

  /* See if we've exceeded max messages and need to disable reading */
  check_read_watch (transport);
  if (unix_transport->read_watch == NULL)
    return;
  
  if (total > unix_transport->max_bytes_read_per_iteration)
    {
      _dbus_verbose ("%d bytes exceeds %d bytes read per iteration, returning\n",
                     total, unix_transport->max_bytes_read_per_iteration);
      goto out;
    }

  if (transport->disconnected)
    goto out;

  if (_dbus_auth_needs_decoding (transport->auth))
    {
      DBusString encoded;

      if (!_dbus_string_init (&encoded, _DBUS_INT_MAX))
        goto out; /* not enough memory for the moment */

      bytes_read = _dbus_read (unix_transport->fd,
                               &encoded,
                               unix_transport->max_bytes_read_per_iteration);

      if (bytes_read > 0)
        {
          int orig_len;
          
          _dbus_message_loader_get_buffer (transport->loader,
                                           &buffer);

          orig_len = _dbus_string_get_length (buffer);
          
          if (!_dbus_auth_decode_data (transport->auth,
                                       &encoded, buffer))
            {
              /* FIXME argh, we are really fucked here - nowhere to
               * put "encoded" while we wait for more memory.  Just
               * screw it for now and disconnect.  The failure may be
               * due to badly-encoded data instead of lack of memory
               * anyhow.
               */
              _dbus_verbose ("Disconnected from remote app due to failure decoding data\n");
              do_io_error (transport);
            }

          _dbus_message_loader_return_buffer (transport->loader,
                                              buffer,
                                              _dbus_string_get_length (buffer) - orig_len);
        }

      _dbus_string_free (&encoded);
    }
  else
    {
      _dbus_message_loader_get_buffer (transport->loader,
                                       &buffer);
      
      bytes_read = _dbus_read (unix_transport->fd,
                               buffer, unix_transport->max_bytes_read_per_iteration);
      
      _dbus_message_loader_return_buffer (transport->loader,
                                          buffer,
                                          bytes_read < 0 ? 0 : bytes_read);
    }
  
  if (bytes_read < 0)
    {
      /* EINTR already handled for us */
      
      if (errno == EAGAIN ||
          errno == EWOULDBLOCK)
        goto out;
      else
        {
          _dbus_verbose ("Error reading from remote app: %s\n",
                         _dbus_strerror (errno));
          do_io_error (transport);
          goto out;
        }
    }
  else if (bytes_read == 0)
    {
      _dbus_verbose ("Disconnected from remote app\n");
      do_io_error (transport);
      goto out;
    }
  else
    {
      _dbus_verbose (" read %d bytes\n", bytes_read);
      
      total += bytes_read;      

      queue_messages (transport);
      
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

  _dbus_assert (watch == unix_transport->read_watch ||
                watch == unix_transport->write_watch);
  
  if (flags & (DBUS_WATCH_HANGUP | DBUS_WATCH_ERROR))
    {
      _dbus_transport_disconnect (transport);
      return;
    }
  
  if (watch == unix_transport->read_watch &&
      (flags & DBUS_WATCH_READABLE))
    {
#if 0
      _dbus_verbose ("handling read watch\n");
#endif
      do_authentication (transport, TRUE, FALSE);
      do_reading (transport);
    }
  else if (watch == unix_transport->write_watch &&
           (flags & DBUS_WATCH_WRITABLE))
    {
#if 0
      _dbus_verbose ("handling write watch\n");
#endif
      do_authentication (transport, FALSE, TRUE);
      do_writing (transport);
    }
}

static void
unix_disconnect (DBusTransport *transport)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  
  free_watches (transport);
  
  _dbus_close (unix_transport->fd, NULL);
  unix_transport->fd = -1;
}

static dbus_bool_t
unix_connection_set (DBusTransport *transport)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  
  if (!_dbus_connection_add_watch (transport->connection,
                                   unix_transport->write_watch))
    return FALSE;

  if (!_dbus_connection_add_watch (transport->connection,
                                   unix_transport->read_watch))
    {
      _dbus_connection_remove_watch (transport->connection,
                                     unix_transport->write_watch);
      return FALSE;
    }

  check_read_watch (transport);
  check_write_watch (transport);

  return TRUE;
}

static void
unix_messages_pending (DBusTransport *transport,
                       int            messages_pending)
{
  check_write_watch (transport);
}

/* FIXME use _dbus_poll(), not select() */
/**
 * @todo We need to have a way to wake up the select sleep if
 * a new iteration request comes in with a flag (read/write) that
 * we're not currently serving. Otherwise a call that just reads
 * could block a write call forever (if there are no incoming
 * messages).
 */
static  void
unix_do_iteration (DBusTransport *transport,
                   unsigned int   flags,
                   int            timeout_milliseconds)
{
  DBusTransportUnix *unix_transport = (DBusTransportUnix*) transport;
  fd_set read_set;
  fd_set write_set;
  dbus_bool_t do_select;
  int select_res;

  _dbus_verbose (" iteration flags = %s%s timeout = %d read_watch = %p write_watch = %p\n",
                 flags & DBUS_ITERATION_DO_READING ? "read" : "",
                 flags & DBUS_ITERATION_DO_WRITING ? "write" : "",
                 timeout_milliseconds,
                 unix_transport->read_watch,
                 unix_transport->write_watch);
  
  /* "again" has to be up here because on EINTR the fd sets become
   * undefined
   */
 again:
  
  do_select = FALSE;

  /* the passed in DO_READING/DO_WRITING flags indicate whether to
   * read/write messages, but regardless of those we may need to block
   * for reading/writing to do auth.  But if we do reading for auth,
   * we don't want to read any messages yet if not given DO_READING.
   *
   * Also, if read_watch == NULL or write_watch == NULL, we don't
   * want to read/write so don't.
   */

  FD_ZERO (&read_set);
  FD_ZERO (&write_set);
  
  if (_dbus_transport_get_is_authenticated (transport))
    {
      if (unix_transport->read_watch &&
          (flags & DBUS_ITERATION_DO_READING))
        {
          FD_SET (unix_transport->fd, &read_set);
          do_select = TRUE;
        }
      
      if (unix_transport->write_watch &&
          (flags & DBUS_ITERATION_DO_WRITING))
        {
          FD_SET (unix_transport->fd, &write_set);
          do_select = TRUE;
        }
    }
  else
    {
      DBusAuthState auth_state;
      
      auth_state = _dbus_auth_do_work (transport->auth);

      if (transport->receive_credentials_pending ||
          auth_state == DBUS_AUTH_STATE_WAITING_FOR_INPUT)
        {
          FD_SET (unix_transport->fd, &read_set);
          do_select = TRUE;
        }

      if (transport->send_credentials_pending ||
          auth_state == DBUS_AUTH_STATE_HAVE_BYTES_TO_SEND)
        {
          FD_SET (unix_transport->fd, &write_set);
          do_select = TRUE;
        }
    } 

  if (do_select)
    {
      fd_set err_set;
      struct timeval timeout;
      dbus_bool_t use_timeout;
      
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

      /* For blocking selects we drop the connection lock here
       * to avoid blocking out connection access during a potentially
       * indefinite blocking call. The io path is still protected
       * by the io_path_cond condvar, so we won't reenter this.
       */
      if (flags & DBUS_ITERATION_BLOCK)
	_dbus_connection_unlock (transport->connection);
      
      select_res = select (unix_transport->fd + 1,
			   &read_set, &write_set, &err_set,
			   use_timeout ? &timeout : NULL);

      if (flags & DBUS_ITERATION_BLOCK)
	_dbus_connection_lock (transport->connection);
      
      
      if (select_res >= 0)
        {
          if (FD_ISSET (unix_transport->fd, &err_set))
            do_io_error (transport);
          else
            {
              dbus_bool_t need_read = FD_ISSET (unix_transport->fd, &read_set);
              dbus_bool_t need_write = FD_ISSET (unix_transport->fd, &write_set);

              _dbus_verbose ("in iteration, need_read=%d need_write=%d\n",
                             need_read, need_write);
              do_authentication (transport, need_read, need_write);
                                 
              if (need_read && (flags & DBUS_ITERATION_DO_READING))
                do_reading (transport);
              if (need_write && (flags & DBUS_ITERATION_DO_WRITING))
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

static void
unix_live_messages_changed (DBusTransport *transport)
{
  /* See if we should look for incoming messages again */
  check_read_watch (transport);
}

static DBusTransportVTable unix_vtable = {
  unix_finalize,
  unix_handle_watch,
  unix_disconnect,
  unix_connection_set,
  unix_messages_pending,
  unix_do_iteration,
  unix_live_messages_changed
};

/**
 * Creates a new transport for the given file descriptor.  The file
 * descriptor must be nonblocking (use _dbus_set_fd_nonblocking() to
 * make it so). This function is shared by various transports that
 * boil down to a full duplex file descriptor.
 *
 * @param fd the file descriptor.
 * @param server #TRUE if this transport is on the server side of a connection
 * @returns the new transport, or #NULL if no memory.
 */
DBusTransport*
_dbus_transport_new_for_fd (int         fd,
                            dbus_bool_t server)
{
  DBusTransportUnix *unix_transport;
  
  unix_transport = dbus_new0 (DBusTransportUnix, 1);
  if (unix_transport == NULL)
    return NULL;

  if (!_dbus_string_init (&unix_transport->encoded_message,
                          _DBUS_INT_MAX))
    goto failed_0;

  unix_transport->write_watch = _dbus_watch_new (fd,
                                                 DBUS_WATCH_WRITABLE,
                                                 FALSE);
  if (unix_transport->write_watch == NULL)
    goto failed_1;
  
  unix_transport->read_watch = _dbus_watch_new (fd,
                                                DBUS_WATCH_READABLE,
                                                FALSE);
  if (unix_transport->read_watch == NULL)
    goto failed_2;
  
  if (!_dbus_transport_init_base (&unix_transport->base,
                                  &unix_vtable,
                                  server))
    goto failed_3;
  
  unix_transport->fd = fd;
  unix_transport->message_bytes_written = 0;
  
  /* These values should probably be tunable or something. */     
  unix_transport->max_bytes_read_per_iteration = 2048;
  unix_transport->max_bytes_written_per_iteration = 2048;
  
  return (DBusTransport*) unix_transport;

 failed_3:
  _dbus_watch_unref (unix_transport->read_watch);
 failed_2:
  _dbus_watch_unref (unix_transport->write_watch);
 failed_1:
  _dbus_string_free (&unix_transport->encoded_message);  
 failed_0:
  dbus_free (unix_transport);
  return NULL;
}

/**
 * Creates a new transport for the given Unix domain socket
 * path.
 *
 * @param path the path to the domain socket.
 * @param server #TRUE if this transport is on the server side of a connection
 * @param result location to store reason for failure.
 * @returns a new transport, or #NULL on failure.
 */
DBusTransport*
_dbus_transport_new_for_domain_socket (const char     *path,
                                       dbus_bool_t     server,
                                       DBusResultCode *result)
{
  int fd;
  DBusTransport *transport;

  fd = _dbus_connect_unix_socket (path, result);
  if (fd < 0)
    return NULL;

  _dbus_fd_set_close_on_exec (fd);
  
  _dbus_verbose ("Successfully connected to unix socket %s\n",
                 path);
  
  transport = _dbus_transport_new_for_fd (fd, server);
  if (transport == NULL)
    {
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      _dbus_close (fd, NULL);
      fd = -1;
    }
  
  return transport;
}

/**
 * Creates a new transport for the given hostname and port.
 *
 * @param host the host to connect to
 * @param port the port to connect to
 * @param server #TRUE if this transport is on the server side of a connection
 * @param result location to store reason for failure.
 * @returns a new transport, or #NULL on failure.
 */
DBusTransport*
_dbus_transport_new_for_tcp_socket (const char     *host,
                                    dbus_int32_t    port,
                                    dbus_bool_t     server,
                                    DBusResultCode *result)
{
  int fd;
  DBusTransport *transport;
  
  fd = _dbus_connect_tcp_socket (host, port, result);
  if (fd < 0)
    return NULL;

  _dbus_fd_set_close_on_exec (fd);
  
  _dbus_verbose ("Successfully connected to tcp socket %s:%d\n",
                 host, port);
  
  transport = _dbus_transport_new_for_fd (fd, server);
  if (transport == NULL)
    {
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      _dbus_close (fd, NULL);
      fd = -1;
    }

  return transport;
}

/** @} */


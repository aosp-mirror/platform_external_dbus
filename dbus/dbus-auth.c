/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-auth.c Authentication
 *
 * Copyright (C) 2002, 2003 Red Hat Inc.
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
#include "dbus-auth.h"
#include "dbus-string.h"
#include "dbus-list.h"
#include "dbus-internals.h"

/* See doc/dbus-sasl-profile.txt */

/**
 * @defgroup DBusAuth Authentication
 * @ingroup  DBusInternals
 * @brief DBusAuth object
 *
 * DBusAuth manages the authentication negotiation when a connection
 * is first established, and also manage any encryption used over a
 * connection.
 *
 * The file doc/dbus-sasl-profile.txt documents the network protocol
 * used for authentication.
 *
 * @todo some SASL profiles require sending the empty string as a
 * challenge/response, but we don't currently allow that in our
 * protocol.
 */

/**
 * @defgroup DBusAuthInternals Authentication implementation details
 * @ingroup  DBusInternals
 * @brief DBusAuth implementation details
 *
 * Private details of authentication code.
 *
 * @{
 */

/**
 * Processes a command. Returns whether we had enough memory to
 * complete the operation.
 */
typedef dbus_bool_t (* DBusProcessAuthCommandFunction) (DBusAuth         *auth,
                                                        const DBusString *command,
                                                        const DBusString *args);

typedef struct
{
  const char *command;
  DBusProcessAuthCommandFunction func;
} DBusAuthCommandHandler;

/**
 * This function appends an initial client response to the given string
 */
typedef dbus_bool_t (* DBusInitialResponseFunction)  (DBusAuth         *auth,
                                                      DBusString       *response);

/**
 * This function processes a block of data received from the peer.
 * i.e. handles a DATA command.
 */
typedef dbus_bool_t (* DBusAuthDataFunction)     (DBusAuth         *auth,
                                                  const DBusString *data);

/**
 * This function encodes a block of data from the peer.
 */
typedef dbus_bool_t (* DBusAuthEncodeFunction)   (DBusAuth         *auth,
                                                  const DBusString *data,
                                                  DBusString       *encoded);

/**
 * This function decodes a block of data from the peer.
 */
typedef dbus_bool_t (* DBusAuthDecodeFunction)   (DBusAuth         *auth,
                                                  const DBusString *data,
                                                  DBusString       *decoded);

/**
 * This function is called when the mechanism is abandoned.
 */
typedef void        (* DBusAuthShutdownFunction) (DBusAuth       *auth);

typedef struct
{
  const char *mechanism;
  DBusAuthDataFunction server_data_func;
  DBusAuthEncodeFunction server_encode_func;
  DBusAuthDecodeFunction server_decode_func;
  DBusAuthShutdownFunction server_shutdown_func;
  DBusInitialResponseFunction client_initial_response_func;
  DBusAuthDataFunction client_data_func;
  DBusAuthEncodeFunction client_encode_func;
  DBusAuthDecodeFunction client_decode_func;
  DBusAuthShutdownFunction client_shutdown_func;
} DBusAuthMechanismHandler;

/**
 * Internal members of DBusAuth.
 */
struct DBusAuth
{
  int refcount;           /**< reference count */

  DBusString incoming;    /**< Incoming data buffer */
  DBusString outgoing;    /**< Outgoing data buffer */
  
  const DBusAuthCommandHandler *handlers; /**< Handlers for commands */

  const DBusAuthMechanismHandler *mech;   /**< Current auth mechanism */

  DBusString identity;                   /**< Current identity we're authorizing
                                          *   as.
                                          */
  
  DBusCredentials credentials;      /**< Credentials, fields may be -1 */

  DBusCredentials authorized_identity; /**< Credentials that are authorized */
  
  unsigned int needed_memory : 1;   /**< We needed memory to continue since last
                                     * successful getting something done
                                     */
  unsigned int need_disconnect : 1; /**< We've given up, time to disconnect */
  unsigned int authenticated : 1;   /**< We are authenticated */
  unsigned int authenticated_pending_output : 1; /**< Authenticated once we clear outgoing buffer */
  unsigned int authenticated_pending_begin : 1;  /**< Authenticated once we get BEGIN */
  unsigned int already_got_mechanisms : 1;       /**< Client already got mech list */
  unsigned int already_asked_for_initial_response : 1; /**< Already sent a blank challenge to get an initial response */
};

typedef struct
{
  DBusAuth base;

  DBusList *mechs_to_try; /**< Mechanisms we got from the server that we're going to try using */
  
} DBusAuthClient;

typedef struct
{
  DBusAuth base;

  int failures;     /**< Number of times client has been rejected */
  int max_failures; /**< Number of times we reject before disconnect */
  
} DBusAuthServer;

static dbus_bool_t process_auth         (DBusAuth         *auth,
                                         const DBusString *command,
                                         const DBusString *args);
static dbus_bool_t process_cancel       (DBusAuth         *auth,
                                         const DBusString *command,
                                         const DBusString *args);
static dbus_bool_t process_begin        (DBusAuth         *auth,
                                         const DBusString *command,
                                         const DBusString *args);
static dbus_bool_t process_data_server  (DBusAuth         *auth,
                                         const DBusString *command,
                                         const DBusString *args);
static dbus_bool_t process_error_server (DBusAuth         *auth,
                                         const DBusString *command,
                                         const DBusString *args);
static dbus_bool_t process_rejected     (DBusAuth         *auth,
                                         const DBusString *command,
                                         const DBusString *args);
static dbus_bool_t process_ok           (DBusAuth         *auth,
                                         const DBusString *command,
                                         const DBusString *args);
static dbus_bool_t process_data_client  (DBusAuth         *auth,
                                         const DBusString *command,
                                         const DBusString *args);
static dbus_bool_t process_error_client (DBusAuth         *auth,
                                         const DBusString *command,
                                         const DBusString *args);


static dbus_bool_t client_try_next_mechanism (DBusAuth *auth);
static dbus_bool_t send_rejected             (DBusAuth *auth);

static DBusAuthCommandHandler
server_handlers[] = {
  { "AUTH", process_auth },
  { "CANCEL", process_cancel },
  { "BEGIN", process_begin },
  { "DATA", process_data_server },
  { "ERROR", process_error_server },
  { NULL, NULL }
};

static DBusAuthCommandHandler
client_handlers[] = {
  { "REJECTED", process_rejected },
  { "OK", process_ok },
  { "DATA", process_data_client },
  { "ERROR", process_error_client },
  { NULL, NULL }
};

/**
 * @param auth the auth conversation
 * @returns #TRUE if the conversation is the server side
 */
#define DBUS_AUTH_IS_SERVER(auth) ((auth)->handlers == server_handlers)
/**
 * @param auth the auth conversation
 * @returns #TRUE if the conversation is the client side
 */
#define DBUS_AUTH_IS_CLIENT(auth) ((auth)->handlers == client_handlers)
/**
 * @param auth the auth conversation
 * @returns auth cast to DBusAuthClient
 */
#define DBUS_AUTH_CLIENT(auth)    ((DBusAuthClient*)(auth))
/**
 * @param auth the auth conversation
 * @returns auth cast to DBusAuthServer
 */
#define DBUS_AUTH_SERVER(auth)    ((DBusAuthServer*)(auth))

static DBusAuth*
_dbus_auth_new (int size)
{
  DBusAuth *auth;
  
  auth = dbus_malloc0 (size);
  if (auth == NULL)
    return NULL;
  
  auth->refcount = 1;

  auth->credentials.pid = -1;
  auth->credentials.uid = -1;
  auth->credentials.gid = -1;

  auth->authorized_identity.pid = -1;
  auth->authorized_identity.uid = -1;
  auth->authorized_identity.gid = -1;
  
  /* note that we don't use the max string length feature,
   * because you can't use that feature if you're going to
   * try to recover from out-of-memory (it creates
   * what looks like unrecoverable inability to alloc
   * more space in the string). But we do handle
   * overlong buffers in _dbus_auth_do_work().
   */
  
  if (!_dbus_string_init (&auth->incoming, _DBUS_INT_MAX))
    {
      dbus_free (auth);
      return NULL;
    }

  if (!_dbus_string_init (&auth->outgoing, _DBUS_INT_MAX))
    {
      _dbus_string_free (&auth->incoming);
      dbus_free (auth);
      return NULL;
    }
  
  if (!_dbus_string_init (&auth->identity, _DBUS_INT_MAX))
    {
      _dbus_string_free (&auth->incoming);
      _dbus_string_free (&auth->outgoing);
      dbus_free (auth);
      return NULL;
    }
  
  return auth;
}

static void
shutdown_mech (DBusAuth *auth)
{
  /* Cancel any auth */
  auth->authenticated_pending_begin = FALSE;
  auth->authenticated = FALSE;
  auth->already_asked_for_initial_response = FALSE;
  _dbus_string_set_length (&auth->identity, 0);
  auth->authorized_identity.pid = -1;
  auth->authorized_identity.uid = -1;
  auth->authorized_identity.gid = -1;
  
  if (auth->mech != NULL)
    {
      _dbus_verbose ("Shutting down mechanism %s\n",
                     auth->mech->mechanism);
      
      if (DBUS_AUTH_IS_CLIENT (auth))
        (* auth->mech->client_shutdown_func) (auth);
      else
        (* auth->mech->server_shutdown_func) (auth);
      
      auth->mech = NULL;
    }
}

static dbus_bool_t
handle_server_data_stupid_test_mech (DBusAuth         *auth,
                                     const DBusString *data)
{
  if (!_dbus_string_append (&auth->outgoing,
                            "OK\r\n"))
    return FALSE;

  auth->authenticated_pending_begin = TRUE;
  
  return TRUE;
}

static void
handle_server_shutdown_stupid_test_mech (DBusAuth *auth)
{

}

static dbus_bool_t
handle_client_data_stupid_test_mech (DBusAuth         *auth,
                                     const DBusString *data)
{
  
  return TRUE;
}

static void
handle_client_shutdown_stupid_test_mech (DBusAuth *auth)
{

}

/* the stupid test mech is a base64-encoded string;
 * all the inefficiency, none of the security!
 */
static dbus_bool_t
handle_encode_stupid_test_mech (DBusAuth         *auth,
                                const DBusString *plaintext,
                                DBusString       *encoded)
{
  if (!_dbus_string_base64_encode (plaintext, 0, encoded,
                                   _dbus_string_get_length (encoded)))
    return FALSE;
  
  return TRUE;
}

static dbus_bool_t
handle_decode_stupid_test_mech (DBusAuth         *auth,
                                const DBusString *encoded,
                                DBusString       *plaintext)
{
  if (!_dbus_string_base64_decode (encoded, 0, plaintext,
                                   _dbus_string_get_length (plaintext)))
    return FALSE;
  
  return TRUE;
}

static dbus_bool_t
handle_server_data_external_mech (DBusAuth         *auth,
                                  const DBusString *data)
{
  DBusCredentials desired_identity;

  if (auth->credentials.uid < 0)
    {
      _dbus_verbose ("no credentials, mechanism EXTERNAL can't authenticate\n");
      return send_rejected (auth);
    }
  
  if (_dbus_string_get_length (data) > 0)
    {
      if (_dbus_string_get_length (&auth->identity) > 0)
        {
          /* Tried to send two auth identities, wtf */
          _dbus_verbose ("client tried to send auth identity, but we already have one\n");
          return send_rejected (auth);
        }
      else
        {
          /* this is our auth identity */
          if (!_dbus_string_copy (data, 0, &auth->identity, 0))
            return FALSE;
        }
    }

  /* Poke client for an auth identity, if none given */
  if (_dbus_string_get_length (&auth->identity) == 0 &&
      !auth->already_asked_for_initial_response)
    {
      if (_dbus_string_append (&auth->outgoing,
                               "DATA\r\n"))
        {
          _dbus_verbose ("sending empty challenge asking client for auth identity\n");
          auth->already_asked_for_initial_response = TRUE;
          return TRUE;
        }
      else
        return FALSE;
    }

  desired_identity.pid = -1;
  desired_identity.uid = -1;
  desired_identity.gid = -1;
  
  /* If auth->identity is still empty here, then client
   * responded with an empty string after we poked it for
   * an initial response. This means to try to auth the
   * identity provided in the credentials.
   */
  if (_dbus_string_get_length (&auth->identity) == 0)
    {
      desired_identity.uid = auth->credentials.uid;
    }
  else
    {
      if (!_dbus_credentials_from_uid_string (&auth->identity,
                                              &desired_identity))
        {
          _dbus_verbose ("could not get credentials from uid string\n");
          return send_rejected (auth);
        }
    }

  if (desired_identity.uid < 0)
    {
      _dbus_verbose ("desired UID %d is no good\n", desired_identity.uid);
      return send_rejected (auth);
    }
  
  if (_dbus_credentials_match (&desired_identity,
                               &auth->credentials))
    {
      /* client has authenticated */
      _dbus_verbose ("authenticated client with UID %d matching socket credentials UID %d\n",
                     desired_identity.uid,
                     auth->credentials.uid);
      
      if (!_dbus_string_append (&auth->outgoing,
                                "OK\r\n"))
        return FALSE;

      auth->authorized_identity.uid = desired_identity.uid;
      
      auth->authenticated_pending_begin = TRUE;
      
      return TRUE;
    }
  else
    {
      _dbus_verbose ("credentials uid=%d gid=%d do not allow uid=%d gid=%d\n",
                     auth->credentials.uid, auth->credentials.gid,
                     desired_identity.uid, desired_identity.gid);
      return send_rejected (auth);
    }
}

static void
handle_server_shutdown_external_mech (DBusAuth *auth)
{

}

static dbus_bool_t
handle_client_initial_response_external_mech (DBusAuth         *auth,
                                              DBusString       *response)
{
  /* We always append our UID as an initial response, so the server
   * doesn't have to send back an empty challenge to check whether we
   * want to specify an identity. i.e. this avoids a round trip that
   * the spec for the EXTERNAL mechanism otherwise requires.
   */
  DBusString plaintext;

  if (!_dbus_string_init (&plaintext, _DBUS_INT_MAX))
    return FALSE;
  
  if (!_dbus_string_append_our_uid (&plaintext))
    goto failed;

  if (!_dbus_string_base64_encode (&plaintext, 0,
                                   response,
                                   _dbus_string_get_length (response)))
    goto failed;

  _dbus_string_free (&plaintext);
  
  return TRUE;

 failed:
  _dbus_string_free (&plaintext);
  return FALSE;  
}

static dbus_bool_t
handle_client_data_external_mech (DBusAuth         *auth,
                                  const DBusString *data)
{
  
  return TRUE;
}

static void
handle_client_shutdown_external_mech (DBusAuth *auth)
{

}

/* Put mechanisms here in order of preference.
 * What I eventually want to have is:
 *
 *  - a mechanism that checks UNIX domain socket credentials
 *  - a simple magic cookie mechanism like X11 or ICE
 *  - mechanisms that chain to Cyrus SASL, so we can use anything it
 *    offers such as Kerberos, X509, whatever.
 * 
 */
static const DBusAuthMechanismHandler
all_mechanisms[] = {
  { "EXTERNAL",
    handle_server_data_external_mech,
    NULL, NULL,
    handle_server_shutdown_external_mech,
    handle_client_initial_response_external_mech,
    handle_client_data_external_mech,
    NULL, NULL,
    handle_client_shutdown_external_mech },
  /* Obviously this has to die for production use */
  { "DBUS_STUPID_TEST_MECH",
    handle_server_data_stupid_test_mech,
    handle_encode_stupid_test_mech,
    handle_decode_stupid_test_mech,
    handle_server_shutdown_stupid_test_mech,
    NULL,
    handle_client_data_stupid_test_mech,
    handle_encode_stupid_test_mech,
    handle_decode_stupid_test_mech,
    handle_client_shutdown_stupid_test_mech },
  { NULL, NULL }
};

static const DBusAuthMechanismHandler*
find_mech (const DBusString *name)
{
  int i;
  
  i = 0;
  while (all_mechanisms[i].mechanism != NULL)
    {
      if (_dbus_string_equal_c_str (name,
                                    all_mechanisms[i].mechanism))

        return &all_mechanisms[i];
      
      ++i;
    }
  
  return NULL;
}

static dbus_bool_t
send_rejected (DBusAuth *auth)
{
  DBusString command;
  DBusAuthServer *server_auth;
  int i;
  
  if (!_dbus_string_init (&command, _DBUS_INT_MAX))
    return FALSE;
  
  if (!_dbus_string_append (&command,
                            "REJECTED"))
    goto nomem;

  i = 0;
  while (all_mechanisms[i].mechanism != NULL)
    {
      if (!_dbus_string_append (&command,
                                " "))
        goto nomem;

      if (!_dbus_string_append (&command,
                                all_mechanisms[i].mechanism))
        goto nomem;
      
      ++i;
    }
  
  if (!_dbus_string_append (&command, "\r\n"))
    goto nomem;

  if (!_dbus_string_copy (&command, 0, &auth->outgoing,
                          _dbus_string_get_length (&auth->outgoing)))
    goto nomem;

  shutdown_mech (auth);
  
  _dbus_assert (DBUS_AUTH_IS_SERVER (auth));
  server_auth = DBUS_AUTH_SERVER (auth);
  server_auth->failures += 1;
  
  return TRUE;

 nomem:
  _dbus_string_free (&command);
  return FALSE;
}

static dbus_bool_t
process_auth (DBusAuth         *auth,
              const DBusString *command,
              const DBusString *args)
{
  if (auth->mech)
    {
      /* We are already using a mechanism, client is on crack */
      if (!_dbus_string_append (&auth->outgoing,
                                "ERROR \"Sent AUTH while another AUTH in progress\"\r\n"))
        return FALSE;

      return TRUE;
    }
  else if (_dbus_string_get_length (args) == 0)
    {
      /* No args to the auth, send mechanisms */
      if (!send_rejected (auth))
        return FALSE;

      return TRUE;
    }
  else
    {
      int i;
      DBusString mech;
      DBusString base64_response;
      DBusString decoded_response;
      
      _dbus_string_find_blank (args, 0, &i);

      if (!_dbus_string_init (&mech, _DBUS_INT_MAX))
        return FALSE;

      if (!_dbus_string_init (&base64_response, _DBUS_INT_MAX))
        {
          _dbus_string_free (&mech);
          return FALSE;
        }

      if (!_dbus_string_init (&decoded_response, _DBUS_INT_MAX))
        {
          _dbus_string_free (&mech);
          _dbus_string_free (&base64_response);
          return FALSE;
        }
      
      if (!_dbus_string_copy_len (args, 0, i, &mech, 0))
        goto failed;

      if (!_dbus_string_copy (args, i, &base64_response, 0))
        goto failed;

      if (!_dbus_string_base64_decode (&base64_response, 0,
                                       &decoded_response, 0))
        goto failed;

      auth->mech = find_mech (&mech);
      if (auth->mech != NULL)
        {
          _dbus_verbose ("Trying mechanism %s with initial response of %d bytes\n",
                         auth->mech->mechanism,
                         _dbus_string_get_length (&decoded_response));
          
          if (!(* auth->mech->server_data_func) (auth,
                                                 &decoded_response))
            goto failed;
        }
      else
        {
          /* Unsupported mechanism */
          if (!send_rejected (auth))
            return FALSE;
        }

      _dbus_string_free (&mech);      
      _dbus_string_free (&base64_response);
      _dbus_string_free (&decoded_response);

      return TRUE;
      
    failed:
      auth->mech = NULL;
      _dbus_string_free (&mech);
      _dbus_string_free (&base64_response);
      _dbus_string_free (&decoded_response);
      return FALSE;
    }
}

static dbus_bool_t
process_cancel (DBusAuth         *auth,
                const DBusString *command,
                const DBusString *args)
{
  shutdown_mech (auth);
  
  return TRUE;
}

static dbus_bool_t
process_begin (DBusAuth         *auth,
               const DBusString *command,
               const DBusString *args)
{
  if (auth->authenticated_pending_begin)
    auth->authenticated = TRUE;
  else
    {
      auth->need_disconnect = TRUE; /* client trying to send data before auth,
                                     * kick it
                                     */
      shutdown_mech (auth);
    }
  
  return TRUE;
}

static dbus_bool_t
process_data_server (DBusAuth         *auth,
                     const DBusString *command,
                     const DBusString *args)
{
  if (auth->mech != NULL)
    {
      DBusString decoded;

      if (!_dbus_string_init (&decoded, _DBUS_INT_MAX))
        return FALSE;

      if (!_dbus_string_base64_decode (args, 0, &decoded, 0))
        {
          _dbus_string_free (&decoded);
          return FALSE;
        }
      
      if (!(* auth->mech->server_data_func) (auth, &decoded))
        {
          _dbus_string_free (&decoded);
          return FALSE;
        }

      _dbus_string_free (&decoded);
    }
  else
    {
      if (!_dbus_string_append (&auth->outgoing,
                                "ERROR \"Not currently in an auth conversation\"\r\n"))
        return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
process_error_server (DBusAuth         *auth,
                      const DBusString *command,
                      const DBusString *args)
{
  
  return TRUE;
}

/* return FALSE if no memory, TRUE if all OK */
static dbus_bool_t
get_word (const DBusString *str,
          int              *start,
          DBusString       *word)
{
  int i;

  _dbus_string_skip_blank (str, *start, start);
  _dbus_string_find_blank (str, *start, &i);
  
  if (i > *start)
    {
      if (!_dbus_string_copy_len (str, *start, i, word, 0))
        return FALSE;
      
      *start = i;
    }

  return TRUE;
}

static dbus_bool_t
record_mechanisms (DBusAuth         *auth,
                   const DBusString *command,
                   const DBusString *args)
{
  int next;
  int len;

  if (auth->already_got_mechanisms)
    return TRUE;
  
  len = _dbus_string_get_length (args);
  
  next = 0;
  while (next < len)
    {
      DBusString m;
      const DBusAuthMechanismHandler *mech;
      
      if (!_dbus_string_init (&m, _DBUS_INT_MAX))
        goto nomem;
      
      if (!get_word (args, &next, &m))
        goto nomem;

      mech = find_mech (&m);

      if (mech != NULL)
        {
          /* FIXME right now we try mechanisms in the order
           * the server lists them; should we do them in
           * some more deterministic order?
           *
           * Probably in all_mechanisms order, our order of
           * preference. Of course when the server is us,
           * it lists things in that order anyhow.
           */

          _dbus_verbose ("Adding mechanism %s to list we will try\n",
                         mech->mechanism);
          
          if (!_dbus_list_append (& DBUS_AUTH_CLIENT (auth)->mechs_to_try,
                                  (void*) mech))
            goto nomem;
        }
      else
        {
          const char *s;

          _dbus_string_get_const_data (&m, &s);
          _dbus_verbose ("Server offered mechanism \"%s\" that we don't know how to use\n",
                         s);
        }

      _dbus_string_free (&m);
    }
  
  auth->already_got_mechanisms = TRUE;
  
  return TRUE;

 nomem:
  _dbus_list_clear (& DBUS_AUTH_CLIENT (auth)->mechs_to_try);
  
  return FALSE;
}

static dbus_bool_t
client_try_next_mechanism (DBusAuth *auth)
{
  const DBusAuthMechanismHandler *mech;
  DBusString auth_command;

  if (DBUS_AUTH_CLIENT (auth)->mechs_to_try == NULL)
    return FALSE;

  mech = DBUS_AUTH_CLIENT (auth)->mechs_to_try->data;

  if (!_dbus_string_init (&auth_command, _DBUS_INT_MAX))
    return FALSE;
      
  if (!_dbus_string_append (&auth_command,
                            "AUTH "))
    {
      _dbus_string_free (&auth_command);
      return FALSE;
    }  
  
  if (!_dbus_string_append (&auth_command,
                            mech->mechanism))
    {
      _dbus_string_free (&auth_command);
      return FALSE;
    }

  if (mech->client_initial_response_func != NULL)
    {
      if (!_dbus_string_append (&auth_command, " "))
        {
          _dbus_string_free (&auth_command);
          return FALSE;
        }
      
      if (!(* mech->client_initial_response_func) (auth, &auth_command))
        {
          _dbus_string_free (&auth_command);
          return FALSE;
        }
    }
  
  if (!_dbus_string_append (&auth_command,
                            "\r\n"))
    {
      _dbus_string_free (&auth_command);
      return FALSE;
    }

  if (!_dbus_string_copy (&auth_command, 0,
                          &auth->outgoing,
                          _dbus_string_get_length (&auth->outgoing)))
    {
      _dbus_string_free (&auth_command);
      return FALSE;
    }

  auth->mech = mech;      
  _dbus_list_pop_first (& DBUS_AUTH_CLIENT (auth)->mechs_to_try);

  _dbus_verbose ("Trying mechanism %s\n",
                 auth->mech->mechanism);

  _dbus_string_free (&auth_command);
  
  return TRUE;
}

static dbus_bool_t
process_rejected (DBusAuth         *auth,
                  const DBusString *command,
                  const DBusString *args)
{
  shutdown_mech (auth);
  
  if (!auth->already_got_mechanisms)
    {
      if (!record_mechanisms (auth, command, args))
        return FALSE;
    }
  
  if (DBUS_AUTH_CLIENT (auth)->mechs_to_try != NULL)
    {
      client_try_next_mechanism (auth);
    }
  else
    {
      /* Give up */
      auth->need_disconnect = TRUE;
    }
  
  return TRUE;
}

static dbus_bool_t
process_ok (DBusAuth         *auth,
            const DBusString *command,
            const DBusString *args)
{
  if (!_dbus_string_append (&auth->outgoing,
                            "BEGIN\r\n"))
    return FALSE;
  
  auth->authenticated_pending_output = TRUE;
  
  return TRUE;
}


static dbus_bool_t
process_data_client (DBusAuth         *auth,
                     const DBusString *command,
                     const DBusString *args)
{
  if (auth->mech != NULL)
    {
      DBusString decoded;

      if (!_dbus_string_init (&decoded, _DBUS_INT_MAX))
        return FALSE;

      if (!_dbus_string_base64_decode (args, 0, &decoded, 0))
        {
          _dbus_string_free (&decoded);
          return FALSE;
        }
      
      if (!(* auth->mech->client_data_func) (auth, &decoded))
        {
          _dbus_string_free (&decoded);
          return FALSE;
        }

      _dbus_string_free (&decoded);
    }
  else
    {
      if (!_dbus_string_append (&auth->outgoing,
                                "ERROR \"Got DATA when not in an auth exchange\"\r\n"))
        return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
process_error_client (DBusAuth         *auth,
                      const DBusString *command,
                      const DBusString *args)
{
  return TRUE;
}

static dbus_bool_t
process_unknown (DBusAuth         *auth,
                 const DBusString *command,
                 const DBusString *args)
{
  if (!_dbus_string_append (&auth->outgoing,
                            "ERROR \"Unknown command\"\r\n"))
    return FALSE;

  return TRUE;
}

/* returns whether to call it again right away */
static dbus_bool_t
process_command (DBusAuth *auth)
{
  DBusString command;
  DBusString args;
  int eol;
  int i, j;
  dbus_bool_t retval;

  /* _dbus_verbose ("  trying process_command()\n"); */
  
  retval = FALSE;
  
  eol = 0;
  if (!_dbus_string_find (&auth->incoming, 0, "\r\n", &eol))
    return FALSE;
  
  if (!_dbus_string_init (&command, _DBUS_INT_MAX))
    {
      auth->needed_memory = TRUE;
      return FALSE;
    }

  if (!_dbus_string_init (&args, _DBUS_INT_MAX))
    {
      auth->needed_memory = TRUE;
      return FALSE;
    }
  
  if (eol > _DBUS_ONE_MEGABYTE)
    {
      /* This is a giant line, someone is trying to hose us. */
      if (!_dbus_string_append (&auth->outgoing, "ERROR \"Command too long\"\r\n"))
        goto out;
      else
        goto next_command;
    }

  if (!_dbus_string_copy_len (&auth->incoming, 0, eol, &command, 0))
    goto out;

  if (!_dbus_string_validate_ascii (&command, 0,
                                    _dbus_string_get_length (&command)))
    {
      _dbus_verbose ("Command contained non-ASCII chars or embedded nul\n");
      if (!_dbus_string_append (&auth->outgoing, "ERROR \"Command contained non-ASCII\"\r\n"))
        goto out;
      else
        goto next_command;
    }
  
  {
    const char *q;
    _dbus_string_get_const_data (&command, &q);
    _dbus_verbose ("got command \"%s\"\n", q);
  }
  
  _dbus_string_find_blank (&command, 0, &i);
  _dbus_string_skip_blank (&command, i, &j);

  if (j > i)
    _dbus_string_delete (&command, i, j - i);
  
  if (!_dbus_string_move (&command, i, &args, 0))
    goto out;
  
  i = 0;
  while (auth->handlers[i].command != NULL)
    {
      if (_dbus_string_equal_c_str (&command,
                                    auth->handlers[i].command))
        {
          _dbus_verbose ("Processing auth command %s\n",
                         auth->handlers[i].command);
          
          if (!(* auth->handlers[i].func) (auth, &command, &args))
            goto out;

          break;
        }
      ++i;
    }

  if (auth->handlers[i].command == NULL)
    {
      if (!process_unknown (auth, &command, &args))
        goto out;
    }

 next_command:
  
  /* We've succeeded in processing the whole command so drop it out
   * of the incoming buffer and return TRUE to try another command.
   */

  _dbus_string_delete (&auth->incoming, 0, eol);
  
  /* kill the \r\n */
  _dbus_string_delete (&auth->incoming, 0, 2);

  retval = TRUE;
  
 out:
  _dbus_string_free (&args);
  _dbus_string_free (&command);

  if (!retval)
    auth->needed_memory = TRUE;
  else
    auth->needed_memory = FALSE;
  
  return retval;
}


/** @} */

/**
 * @addtogroup DBusAuth
 * @{
 */

/**
 * Creates a new auth conversation object for the server side.
 * See doc/dbus-sasl-profile.txt for full details on what
 * this object does.
 *
 * @returns the new object or #NULL if no memory
 */
DBusAuth*
_dbus_auth_server_new (void)
{
  DBusAuth *auth;
  DBusAuthServer *server_auth;

  auth = _dbus_auth_new (sizeof (DBusAuthServer));
  if (auth == NULL)
    return NULL;

  auth->handlers = server_handlers;

  server_auth = DBUS_AUTH_SERVER (auth);

  /* perhaps this should be per-mechanism with a lower
   * max
   */
  server_auth->failures = 0;
  server_auth->max_failures = 6;
  
  return auth;
}

/**
 * Creates a new auth conversation object for the client side.
 * See doc/dbus-sasl-profile.txt for full details on what
 * this object does.
 *
 * @returns the new object or #NULL if no memory
 */
DBusAuth*
_dbus_auth_client_new (void)
{
  DBusAuth *auth;

  auth = _dbus_auth_new (sizeof (DBusAuthClient));
  if (auth == NULL)
    return NULL;

  auth->handlers = client_handlers;

  /* Add a default mechanism to try */
  if (!_dbus_list_append (& DBUS_AUTH_CLIENT (auth)->mechs_to_try,
                          (void*) &all_mechanisms[0]))
    {
      _dbus_auth_unref (auth);
      return NULL;
    }

  /* Now try the mechanism we just added */
  if (!client_try_next_mechanism (auth))
    {
      _dbus_auth_unref (auth);
      return NULL;
    }
  
  return auth;
}

/**
 * Increments the refcount of an auth object.
 *
 * @param auth the auth conversation
 */
void
_dbus_auth_ref (DBusAuth *auth)
{
  _dbus_assert (auth != NULL);
  
  auth->refcount += 1;
}

/**
 * Decrements the refcount of an auth object.
 *
 * @param auth the auth conversation
 */
void
_dbus_auth_unref (DBusAuth *auth)
{
  _dbus_assert (auth != NULL);
  _dbus_assert (auth->refcount > 0);

  auth->refcount -= 1;
  if (auth->refcount == 0)
    {
      shutdown_mech (auth);

      if (DBUS_AUTH_IS_CLIENT (auth))
        {
          _dbus_list_clear (& DBUS_AUTH_CLIENT (auth)->mechs_to_try);
        }

      _dbus_string_free (&auth->identity);
      _dbus_string_free (&auth->incoming);
      _dbus_string_free (&auth->outgoing);
      dbus_free (auth);
    }
}

/**
 * @param auth the auth conversation object
 * @returns #TRUE if we're in a final state
 */
#define DBUS_AUTH_IN_END_STATE(auth) ((auth)->need_disconnect || (auth)->authenticated)

/**
 * Analyzes buffered input and moves the auth conversation forward,
 * returning the new state of the auth conversation.
 *
 * @param auth the auth conversation
 * @returns the new state
 */
DBusAuthState
_dbus_auth_do_work (DBusAuth *auth)
{
  auth->needed_memory = FALSE;

  /* Max amount we'll buffer up before deciding someone's on crack */
#define MAX_BUFFER (16 * _DBUS_ONE_KILOBYTE)

  do
    {
      if (DBUS_AUTH_IN_END_STATE (auth))
        break;
      
      if (_dbus_string_get_length (&auth->incoming) > MAX_BUFFER ||
          _dbus_string_get_length (&auth->outgoing) > MAX_BUFFER)
        {
          auth->need_disconnect = TRUE;
          _dbus_verbose ("Disconnecting due to excessive data buffered in auth phase\n");
          break;
        }

      if (auth->mech == NULL &&
          auth->already_got_mechanisms &&
          DBUS_AUTH_CLIENT (auth)->mechs_to_try == NULL)
        {
          auth->need_disconnect = TRUE;
          _dbus_verbose ("Disconnecting because we are out of mechanisms to try using\n");
          break;
        }
    }
  while (process_command (auth));

  if (DBUS_AUTH_IS_SERVER (auth) &&
      DBUS_AUTH_SERVER (auth)->failures >=
      DBUS_AUTH_SERVER (auth)->max_failures)
    auth->need_disconnect = TRUE;

  if (auth->need_disconnect)
    return DBUS_AUTH_STATE_NEED_DISCONNECT;
  else if (auth->authenticated)
    {
      if (_dbus_string_get_length (&auth->incoming) > 0)
        return DBUS_AUTH_STATE_AUTHENTICATED_WITH_UNUSED_BYTES;
      else
        return DBUS_AUTH_STATE_AUTHENTICATED;
    }
  else if (auth->needed_memory)
    return DBUS_AUTH_STATE_WAITING_FOR_MEMORY;
  else if (_dbus_string_get_length (&auth->outgoing) > 0)
    return DBUS_AUTH_STATE_HAVE_BYTES_TO_SEND;
  else
    return DBUS_AUTH_STATE_WAITING_FOR_INPUT;
}

/**
 * Gets bytes that need to be sent to the peer we're conversing with.
 * After writing some bytes, _dbus_auth_bytes_sent() must be called
 * to notify the auth object that they were written.
 *
 * @param auth the auth conversation
 * @param str return location for a ref to the buffer to send
 * @returns #FALSE if nothing to send
 */
dbus_bool_t
_dbus_auth_get_bytes_to_send (DBusAuth          *auth,
                              const DBusString **str)
{
  _dbus_assert (auth != NULL);
  _dbus_assert (str != NULL);

  *str = NULL;
  
  if (DBUS_AUTH_IN_END_STATE (auth))
    return FALSE;

  if (_dbus_string_get_length (&auth->outgoing) == 0)
    return FALSE;

  *str = &auth->outgoing;

  return TRUE;
}

/**
 * Notifies the auth conversation object that
 * the given number of bytes of the outgoing buffer
 * have been written out.
 *
 * @param auth the auth conversation
 * @param bytes_sent number of bytes written out
 */
void
_dbus_auth_bytes_sent (DBusAuth *auth,
                       int       bytes_sent)
{
  _dbus_string_delete (&auth->outgoing,
                       0, bytes_sent);
  
  if (auth->authenticated_pending_output &&
      _dbus_string_get_length (&auth->outgoing) == 0)
    auth->authenticated = TRUE;
}

/**
 * Stores bytes received from the peer we're conversing with.
 *
 * @param auth the auth conversation
 * @param str the received bytes.
 * @returns #FALSE if not enough memory to store the bytes or we were already authenticated.
 */
dbus_bool_t
_dbus_auth_bytes_received (DBusAuth   *auth,
                           const DBusString *str)
{
  _dbus_assert (auth != NULL);
  _dbus_assert (str != NULL);
  
  if (DBUS_AUTH_IN_END_STATE (auth))
    return FALSE;

  auth->needed_memory = FALSE;
  
  if (!_dbus_string_copy (str, 0,
                          &auth->incoming,
                          _dbus_string_get_length (&auth->incoming)))
    {
      auth->needed_memory = TRUE;
      return FALSE;
    }

  _dbus_auth_do_work (auth);
  
  return TRUE;
}

/**
 * Returns leftover bytes that were not used as part of the auth
 * conversation.  These bytes will be part of the message stream
 * instead. This function may not be called until authentication has
 * succeeded.
 *
 * @param auth the auth conversation
 * @param str string to append the unused bytes to
 * @returns #FALSE if not enough memory to return the bytes
 */
dbus_bool_t
_dbus_auth_get_unused_bytes (DBusAuth   *auth,
                             DBusString *str)
{
  if (!DBUS_AUTH_IN_END_STATE (auth))
    return FALSE;
  
  if (!_dbus_string_move (&auth->incoming,
                          0, str,
                          _dbus_string_get_length (str)))
    return FALSE;

  return TRUE;
}

/**
 * Called post-authentication, indicates whether we need to encode
 * the message stream with _dbus_auth_encode_data() prior to
 * sending it to the peer.
 *
 * @param auth the auth conversation
 * @returns #TRUE if we need to encode the stream
 */
dbus_bool_t
_dbus_auth_needs_encoding (DBusAuth *auth)
{
  if (!auth->authenticated)
    return FALSE;
  
  if (auth->mech != NULL)
    {
      if (DBUS_AUTH_IS_CLIENT (auth))
        return auth->mech->client_encode_func != NULL;
      else
        return auth->mech->server_encode_func != NULL;
    }
  else
    return FALSE;
}

/**
 * Called post-authentication, encodes a block of bytes for sending to
 * the peer. If no encoding was negotiated, just copies the bytes
 * (you can avoid this by checking _dbus_auth_needs_encoding()).
 *
 * @param auth the auth conversation
 * @param plaintext the plain text data
 * @param encoded initialized string to where encoded data is appended
 * @returns #TRUE if we had enough memory and successfully encoded
 */
dbus_bool_t
_dbus_auth_encode_data (DBusAuth         *auth,
                        const DBusString *plaintext,
                        DBusString       *encoded)
{
  _dbus_assert (plaintext != encoded);
  
  if (!auth->authenticated)
    return FALSE;
  
  if (_dbus_auth_needs_encoding (auth))
    {
      if (DBUS_AUTH_IS_CLIENT (auth))
        return (* auth->mech->client_encode_func) (auth, plaintext, encoded);
      else
        return (* auth->mech->server_encode_func) (auth, plaintext, encoded);
    }
  else
    {
      return _dbus_string_copy (plaintext, 0, encoded,
                                _dbus_string_get_length (encoded));
    }
}

/**
 * Called post-authentication, indicates whether we need to decode
 * the message stream with _dbus_auth_decode_data() after
 * receiving it from the peer.
 *
 * @param auth the auth conversation
 * @returns #TRUE if we need to encode the stream
 */
dbus_bool_t
_dbus_auth_needs_decoding (DBusAuth *auth)
{
  if (!auth->authenticated)
    return FALSE;
    
  if (auth->mech != NULL)
    {
      if (DBUS_AUTH_IS_CLIENT (auth))
        return auth->mech->client_decode_func != NULL;
      else
        return auth->mech->server_decode_func != NULL;
    }
  else
    return FALSE;
}


/**
 * Called post-authentication, decodes a block of bytes received from
 * the peer. If no encoding was negotiated, just copies the bytes (you
 * can avoid this by checking _dbus_auth_needs_decoding()).
 *
 * @todo We need to be able to distinguish "out of memory" error
 * from "the data is hosed" error.
 *
 * @param auth the auth conversation
 * @param encoded the encoded data
 * @param plaintext initialized string where decoded data is appended
 * @returns #TRUE if we had enough memory and successfully decoded
 */
dbus_bool_t
_dbus_auth_decode_data (DBusAuth         *auth,
                        const DBusString *encoded,
                        DBusString       *plaintext)
{
  _dbus_assert (plaintext != encoded);
  
  if (!auth->authenticated)
    return FALSE;
  
  if (_dbus_auth_needs_decoding (auth))
    {
      if (DBUS_AUTH_IS_CLIENT (auth))
        return (* auth->mech->client_decode_func) (auth, encoded, plaintext);
      else
        return (* auth->mech->server_decode_func) (auth, encoded, plaintext);
    }
  else
    {
      return _dbus_string_copy (encoded, 0, plaintext,
                                _dbus_string_get_length (plaintext));
    }
}

/**
 * Sets credentials received via reliable means from the operating
 * system.
 *
 * @param auth the auth conversation
 * @param credentials the credentials received
 */
void
_dbus_auth_set_credentials (DBusAuth               *auth,
                            const DBusCredentials  *credentials)
{
  auth->credentials = *credentials;
}

/**
 * Gets the identity we authorized the client as.  Apps may have
 * different policies as to what identities they allow.
 *
 * @param auth the auth conversation
 * @param credentials the credentials we've authorized
 */
void
_dbus_auth_get_identity (DBusAuth               *auth,
                         DBusCredentials        *credentials)
{
  if (auth->authenticated)
    {
      *credentials = auth->authorized_identity;
    }
  else
    {
      credentials->pid = -1;
      credentials->uid = -1;
      credentials->gid = -1;
    }
}

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include "dbus-auth-script.h"
#include <stdio.h>

static dbus_bool_t
process_test_subdir (const DBusString          *test_base_dir,
                     const char                *subdir)
{
  DBusString test_directory;
  DBusString filename;
  DBusDirIter *dir;
  dbus_bool_t retval;
  DBusResultCode result;

  retval = FALSE;
  dir = NULL;
  
  if (!_dbus_string_init (&test_directory, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("didn't allocate test_directory\n");

  _dbus_string_init_const (&filename, subdir);
  
  if (!_dbus_string_copy (test_base_dir, 0,
                          &test_directory, 0))
    _dbus_assert_not_reached ("couldn't copy test_base_dir to test_directory");
  
  if (!_dbus_concat_dir_and_file (&test_directory, &filename))    
    _dbus_assert_not_reached ("couldn't allocate full path");

  _dbus_string_free (&filename);
  if (!_dbus_string_init (&filename, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("didn't allocate filename string\n");
  
  dir = _dbus_directory_open (&test_directory, &result);
  if (dir == NULL)
    {
      const char *s;
      _dbus_string_get_const_data (&test_directory, &s);
      _dbus_warn ("Could not open %s: %s\n", s,
                  dbus_result_to_string (result));
      goto failed;
    }

  printf ("Testing:\n");
  
  result = DBUS_RESULT_SUCCESS;
 next:
  while (_dbus_directory_get_next_file (dir, &filename, &result))
    {
      DBusString full_path;
      
      if (!_dbus_string_init (&full_path, _DBUS_INT_MAX))
        _dbus_assert_not_reached ("couldn't init string");

      if (!_dbus_string_copy (&test_directory, 0, &full_path, 0))
        _dbus_assert_not_reached ("couldn't copy dir to full_path");

      if (!_dbus_concat_dir_and_file (&full_path, &filename))
        _dbus_assert_not_reached ("couldn't concat file to dir");

      if (!_dbus_string_ends_with_c_str (&filename, ".auth-script"))
        {
          const char *filename_c;
          _dbus_string_get_const_data (&filename, &filename_c);
          _dbus_verbose ("Skipping non-.auth-script file %s\n",
                         filename_c);
          goto next;
        }

      {
        const char *s;
        _dbus_string_get_const_data (&filename, &s);
        printf ("    %s\n", s);
      }
      
      if (!_dbus_auth_script_run (&full_path))
        {
          _dbus_string_free (&full_path);
          goto failed;
        }
      else
        _dbus_string_free (&full_path);
    }

  if (result != DBUS_RESULT_SUCCESS)
    {
      const char *s;
      _dbus_string_get_const_data (&test_directory, &s);
      _dbus_warn ("Could not get next file in %s: %s\n",
                  s, dbus_result_to_string (result));
      goto failed;
    }
    
  retval = TRUE;
  
 failed:

  if (dir)
    _dbus_directory_close (dir);
  _dbus_string_free (&test_directory);
  _dbus_string_free (&filename);

  return retval;
}

static dbus_bool_t
process_test_dirs (const char *test_data_dir)
{
  DBusString test_directory;
  dbus_bool_t retval;

  retval = FALSE;
  
  _dbus_string_init_const (&test_directory, test_data_dir);

  if (!process_test_subdir (&test_directory, "auth"))
    goto failed;

  retval = TRUE;
  
 failed:

  _dbus_string_free (&test_directory);
  
  return retval;
}

dbus_bool_t
_dbus_auth_test (const char *test_data_dir)
{
  
  if (test_data_dir == NULL)
    return TRUE;
  
  if (!process_test_dirs (test_data_dir))
    return FALSE;

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */

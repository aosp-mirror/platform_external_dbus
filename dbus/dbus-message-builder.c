/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message-builder.c Build messages from text files for testing (internal to D-BUS implementation)
 * 
 * Copyright (C) 2003 Red Hat, Inc.
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
#ifdef DBUS_BUILD_TESTS
#include "dbus-message-builder.h"

/**
 * @defgroup DBusMessageBuilder code for loading test message data
 * @ingroup  DBusInternals
 * @brief code for loading up test data for unit tests
 *
 * The code in here is used for unit testing, it loads
 * up message data from a description in a file.
 *
 * @{
 */

/**
 * Reads the given filename, which should be in "message description
 * language" (look at some examples), and builds up the message data
 * from it.  The message data may be invalid, or valid.
 *
 * The file format is:
 * @code
 *   ALIGN <N> aligns to the given value
 *   UNALIGN skips alignment for the next marshal
 *   BYTE <N> inserts the given integer in [0,255]
 *   SAVE_LENGTH <name> records the current length under the given name
 *   LENGTH <name> inserts the saved length of the same name
 * @endcode
 * 
 * Following commands insert aligned data unless
 * preceded by "UNALIGN":
 * @code
 *   INT32 <N> marshals an INT32
 *   UINT32 <N> marshals a UINT32
 *   DOUBLE <N> marshals a double
 *   STRING "Foo" marshals a string
 * @endcode
 * 
 * @param dest the string to append the message data to
 * @param filename the filename to load
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_message_data_load (DBusString       *dest,
                         const DBusString *filename)
{
  /* FIXME implement */
  
}

/** @} */
#endif /* DBUS_BUILD_TESTS */

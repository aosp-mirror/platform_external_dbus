/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps.h Wrappers around system/libc features (internal to D-BUS implementation)
 * 
 * Copyright (C) 2002  Red Hat, Inc.
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

#ifndef DBUS_SYSDEPS_H
#define DBUS_SYSDEPS_H

#include <dbus/dbus-string.h>

/* this is perhaps bogus, but strcmp() etc. are faster if we use the
 * stuff straight out of string.h, so have this here for now.
 */
#include <string.h>

DBUS_BEGIN_DECLS;

/* The idea of this file is to encapsulate everywhere that we're
 * relying on external libc features, for ease of security
 * auditing. The idea is from vsftpd. This also gives us a chance to
 * make things more convenient to use, e.g.  by reading into a
 * DBusString. Operating system headers aren't intended to be used
 * outside of this file and a limited number of others (such as
 * dbus-memory.c)
 */

void _dbus_abort (void);

const char* _dbus_getenv (const char *varname);

DBUS_END_DECLS;

#endif /* DBUS_SYSDEPS_H */

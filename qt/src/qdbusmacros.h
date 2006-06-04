/* qdbusmessage.h QDBusMessage object
 *
 * Copyright (C) 2005 Harald Fernengel <harry@kdevelop.org>
 * Copyright (C) 2006 Trolltech AS. All rights reserved.
 *    Author: Thiago Macieira <thiago.macieira@trolltech.com>
 *
 * Licensed under the Academic Free License version 2.1
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
 * along with this program; if not, write to the Free Software Foundation
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

/*!
    \file qdbusmacros.h
*/

#ifndef QDBUSMACROS_H
#define QDBUSMACROS_H

#include <QtCore/qglobal.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qvariant.h>

#ifdef QT_NO_MEMBER_TEMPLATES
# error Sorry, you need a compiler with support for template member functions to compile QtDBus.
#endif

#if defined(QDBUS_MAKEDLL)
# define QDBUS_EXPORT Q_DECL_EXPORT
#else
# define QDBUS_EXPORT Q_DECL_IMPORT
#endif

#ifndef Q_MOC_RUN
# define Q_ASYNC
#endif

#ifdef Q_CC_MSVC
#include <QtCore/qlist.h>
#include <QtCore/qset.h>
#include <QtCore/qhash.h>
#include <QtCore/qvector.h>
class QDBusType;
inline uint qHash(const QVariant&)  { Q_ASSERT(0); return 0; }
inline uint qHash(const QDBusType&) { Q_ASSERT(0); return 0; }
#endif

#endif

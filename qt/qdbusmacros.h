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

#ifdef DBUS_COMPILATION
/// \internal
# define QDBUS_EXPORT Q_DECL_EXPORT
#else
/// \internal
# define QDBUS_EXPORT Q_DECL_IMPORT
#endif

#ifndef Q_MOC_RUN
/*!
    \relates QDBusAbstractAdaptor
    \brief Marks a method as "asynchronous"

    The Q_ASYNC macro can be used to mark a method to be called and not wait for it to finish
    processing before returning from QDBusInterface::call. The called method cannot return any
    output arguments and, if it does, any such arguments will be discarded.

    You can use this macro in your own adaptors by placing it before your method's return value
    (which must be "void") in the class declaration, as shown in the example:
    \code
        Q_ASYNC void myMethod();
    \endcode

    Its presence in the method implementation (outside the class declaration) is optional.

    \sa #async, \ref UsingAdaptors
*/
# define Q_ASYNC
#endif
#ifndef QT_NO_KEYWORDS

/*!
    \relates QDBusAbstractAdaptor
    \brief Marks a method as "asynchronous"

    This macro is the same as #Q_ASYNC and is provided as a shorthand. However, it is not defined if
    QT_NO_KEYWORDS is defined, which makes Qt not use its extensions to the C++ language (keywords
    emit, signals, slots).
*/
# define async  Q_ASYNC
#endif

#endif

/* qdbusreply.h QDBusReply object - a reply from D-Bus
 *
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

#ifndef QDBUSREPLY_H
#define QDBUSREPLY_H

#include <QtCore/qglobal.h>
#include <QtCore/qvariant.h>

#include "qdbusmacros.h"
#include "qdbusmessage.h"
#include "qdbuserror.h"

#include "qdbustypehelper_p.h"

template<typename T>
class QDBUS_EXPORT QDBusReply
{
    typedef T Type;
public:
    inline QDBusReply(const QDBusMessage &reply)
        : m_error(reply), m_data(Type())
    {
        if (isSuccess())
            m_data = QDBusTypeHelper<Type>::fromVariant(reply.at(0));
    }
    inline QDBusReply(const QDBusError &error)
        : m_error(error), m_data(Type())
    {
    }    

    inline bool isError() const { return m_error.isValid(); }
    inline bool isSuccess() const { return !m_error.isValid(); }

    inline const QDBusError& error() { return m_error; }

    inline Type value()
    {
        return m_data;
    }

    inline operator Type ()
    {
        return m_data;
    }

    static QDBusReply<T> fromVariant(const QDBusReply<QVariant> &variantReply)
    {
        QDBusReply<T> retval;
        retval.m_error = variantReply.m_error;
        if (retval.isSuccess()) {
            retval.m_data = qvariant_cast<Type>(variantReply.m_data);
            if (!qVariantCanConvert<Type>(variantReply.m_data))
                retval.m_error = QDBusError(QDBusError::InvalidSignature,
                                            QLatin1String("Unexpected reply signature"));
        }
        return retval;
    }

private:
    QDBusError m_error;
    Type m_data;
};

# ifndef Q_QDOC
// specialize for void:
template<>
class QDBUS_EXPORT QDBusReply<void>
{
public:
    inline QDBusReply(const QDBusMessage &reply)
        : m_error(reply)
    {
    }
    inline QDBusReply(const QDBusError &error)
        : m_error(error)
    {
    }    

    inline bool isError() const { return m_error.isValid(); }
    inline bool isSuccess() const { return !m_error.isValid(); }

    inline const QDBusError& error() { return m_error; }

private:
    QDBusError m_error;
};
# endif

#endif

/* qdbusmisc.cpp Miscellaneous routines that didn't fit anywhere else
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

#include <string.h>

#include <QtCore/qvariant.h>
#include <QtCore/qmetaobject.h>

#include "qdbusconnection_p.h"
#include "qdbustypehelper_p.h"

bool qDBusCheckAsyncTag(const char *tag)
{
    if (!tag || !*tag)
        return false;

    const char *p = strstr(tag, "async");
    if (p != NULL &&
        (p == tag || *(p-1) == ' ') &&
        (p[5] == '\0' || p[5] == ' '))
        return true;

    p = strstr(tag, "Q_ASYNC");
    if (p != NULL &&
        (p == tag || *(p-1) == ' ') &&
        (p[7] == '\0' || p[7] == ' '))
        return true;

    return false;
}

int qDBusNameToTypeId(const char *name)
{
    int id = static_cast<int>( QVariant::nameToType(name) );
    if (id == QVariant::UserType)
        id = QMetaType::type(name);

    switch (id) {
    case QVariant::Bool:
    case QVariant::Int:
    case QVariant::UInt:
    case QVariant::Char:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::UChar:
    case QVariant::LongLong:
    case QVariant::ULongLong:
    case QVariant::Double:
    case QVariant::String:
    case QVariant::Date:
    case QVariant::Time:
    case QVariant::DateTime:
    case QVariant::Map:
    case QVariant::StringList:
    case QVariant::ByteArray:
    case QVariant::List:
        return id;

    default:
        if (id == QDBusConnectionPrivate::registerMessageMetaType() ||
            id == QDBusTypeHelper<QVariant>::id() ||
            id == QDBusTypeHelper<bool>::listId() ||
            id == QDBusTypeHelper<short>::listId() ||
            id == QDBusTypeHelper<ushort>::listId() ||
            id == QDBusTypeHelper<int>::listId() ||
            id == QDBusTypeHelper<qlonglong>::listId() ||
            id == QDBusTypeHelper<qulonglong>::listId() ||
            id == QDBusTypeHelper<double>::listId())
            return id;

        return 0;               // invalid
    }
}

// calculates the metatypes for the method
// the slot must have the parameters in the following form:
//  - zero or more value or const-ref parameters of any kind
//  - zero or one const ref of QDBusMessage
//  - zero or more non-const ref parameters
// No parameter may be a template.
// this function returns -1 if the parameters don't match the above form
// this function returns the number of *input* parameters, including the QDBusMessage one if any
// this function does not check the return type, so metaTypes[0] is always 0 and always present
// metaTypes.count() >= retval + 1 in all cases
//
// sig must be the normalised signature for the method
int qDBusParametersForMethod(const QMetaMethod &mm, QList<int>& metaTypes)
{
    QList<QByteArray> parameterTypes = mm.parameterTypes();
    metaTypes.clear();

    metaTypes.append(0);        // return type
    int inputCount = 0;
    bool seenMessage = false;
    foreach (QByteArray type, parameterTypes) {
        if (type.endsWith('*')) {
            //qWarning("Could not parse the method '%s'", mm.signature());
            // pointer?
            return -1;
        }

        if (type.endsWith('&')) {
            type.truncate(type.length() - 1);
            int id = qDBusNameToTypeId(type);
            if (id == 0) {
                //qWarning("Could not parse the method '%s'", mm.signature());
                // invalid type in method parameter list
                return -1;
            }

            metaTypes.append( id );
            seenMessage = true; // it cannot appear anymore anyways
            continue;
        }

        if (seenMessage) {      // && !type.endsWith('&')
            //qWarning("Could not parse the method '%s'", mm.signature());
            // non-output parameters after message or after output params
            return -1;          // not allowed
        }

        int id = qDBusNameToTypeId(type);
        if (id == 0) {
            //qWarning("Could not parse the method '%s'", mm.signature());
            // invalid type in method parameter list
            return -1;
        }
        metaTypes.append(id);
        ++inputCount;

        if (id == QDBusConnectionPrivate::registerMessageMetaType())
            seenMessage = true;
    }

    return inputCount;
}

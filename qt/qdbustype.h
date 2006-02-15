/* -*- C++ -*-
 *
 * Copyright (C) 2005 Thiago Macieira <thiago@kde.org>
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef QDBUSTYPE_H
#define QDBUSTYPE_H

#include <QtCore/qvariant.h>
#include <QtCore/qbytearray.h>
#include <QtCore/qshareddata.h>
#include <QtCore/qlist.h>
#include "qdbusmacros.h"
#include <dbus/dbus.h>

// forward declaration
class QDBusTypeList;

class QDBusTypePrivate;
class QDBUS_EXPORT QDBusType
{
public:
    enum StringFormat
    {
        ConventionalNames,
        QtNames,
        QVariantNames
    };
    
    QDBusType();
    explicit QDBusType(int type);
    explicit QDBusType(QVariant::Type type);
    explicit QDBusType(const char* signature);
    explicit QDBusType(DBusSignatureIter*);
    explicit QDBusType(const QString& str);
    explicit QDBusType(const QByteArray& str);
    QDBusType(const QDBusType& other);

    ~QDBusType();

    QDBusType& operator=(const QDBusType& other);

    QVariant::Type qvariantType() const;

    int dbusType() const;
    QByteArray dbusSignature() const;
    bool isValid() const;
    bool isBasic() const;
    bool isContainer() const;

    QDBusTypeList subTypes() const;

    bool isArray() const;
    QDBusType arrayElement() const;

    bool isMap() const;
    QDBusType mapKey() const;
    QDBusType mapValue() const;

    bool operator==(const QDBusType& other) const;

    QString toString(StringFormat = QtNames) const;

    static QVariant::Type qvariantType(int type);
    static QVariant::Type qvariantType(const char* signature);
    static int dbusType(QVariant::Type);
    static const char* dbusSignature(QVariant::Type);

    enum VariantListMode {
        ListIsArray,
        ListIsStruct
    };
    static QDBusType guessFromVariant(const QVariant &variant, VariantListMode = ListIsArray);

private:
    QSharedDataPointer<QDBusTypePrivate> d;
};

class QDBUS_EXPORT QDBusTypeList: public QList<QDBusType>
{
public:
    inline QDBusTypeList() { }
    inline QDBusTypeList(const QDBusTypeList& other)
        : QList<QDBusType>(other)
        { }
    inline QDBusTypeList(const QList<QDBusType>& other)
        : QList<QDBusType>(other)
        { }
    QDBusTypeList(const char* signature);
    QDBusTypeList(DBusSignatureIter*);

    bool canBeMap() const;

    inline QDBusTypeList& operator<<(const QDBusType& item)
        { QList<QDBusType>::operator<<(item); return *this; }

    QByteArray dbusSignature() const;
};

#endif // QDBUSTYPE_H

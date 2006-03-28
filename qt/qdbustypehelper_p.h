/* qdbuslisthelper_p.h Helper class to convert to and from QVariantList
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

//
//  W A R N I N G
//  -------------
//
// This file is not part of the public API.  This header file may
// change from version to version without notice, or even be
// removed.
//
// We mean it.
//
//

#ifndef QDBUSTYPEHELPERPRIVATE_H
#define QDBUSTYPEHELPERPRIVATE_H

#include <QtCore/qlist.h>
#include <QtCore/qvariant.h>
#include <QtCore/qmetatype.h>

// we're going to support all D-Bus primitive types here:
// uchar -- not needed: QByteArray
// bool
// short
// ushort
// int
// uint
// qlonglong
// qulonglong
// double
// QString -- not needed: QStringList
// QList -- not possible: will use QVariant
// QVariant
// QDBusStruct -- not yet existant
// QMap -- not possible: will use QVariant

inline QDBUS_EXPORT int qDBusMetaTypeId(bool *) { return QVariant::Bool; }
inline QDBUS_EXPORT int qDBusMetaTypeId(uchar *) { return QMetaType::UChar; }
inline QDBUS_EXPORT int qDBusMetaTypeId(short *) { return QMetaType::Short; }
inline QDBUS_EXPORT int qDBusMetaTypeId(ushort *) { return QMetaType::UShort; }
inline QDBUS_EXPORT int qDBusMetaTypeId(int *) { return QVariant::Int; }
inline QDBUS_EXPORT int qDBusMetaTypeId(uint *) { return QVariant::UInt; }
inline QDBUS_EXPORT int qDBusMetaTypeId(qlonglong *) { return QVariant::LongLong; }
inline QDBUS_EXPORT int qDBusMetaTypeId(qulonglong *) { return QVariant::ULongLong; }
inline QDBUS_EXPORT int qDBusMetaTypeId(double *) { return QVariant::Double; }
inline QDBUS_EXPORT int qDBusMetaTypeId(QString *) { return QVariant::String; }
QDBUS_EXPORT int qDBusMetaTypeId(QVariant *);
QDBUS_EXPORT int qDBusMetaTypeId(QList<bool> *);
inline QDBUS_EXPORT int qDBusMetaTypeId(QByteArray *) { return QVariant::ByteArray; }
QDBUS_EXPORT int qDBusMetaTypeId(QList<short> *);
QDBUS_EXPORT int qDBusMetaTypeId(QList<ushort> *);
QDBUS_EXPORT int qDBusMetaTypeId(QList<int> *);
QDBUS_EXPORT int qDBusMetaTypeId(QList<uint> *);
QDBUS_EXPORT int qDBusMetaTypeId(QList<qlonglong> *);
QDBUS_EXPORT int qDBusMetaTypeId(QList<qulonglong> *);
QDBUS_EXPORT int qDBusMetaTypeId(QList<double> *);
inline QDBUS_EXPORT int qDBusMetaTypeId(QStringList *) { return QVariant::StringList; }
inline QDBUS_EXPORT int qDBusMetaTypeId(QVariantList *) { return QVariant::List; }
inline QDBUS_EXPORT int qDBusMetaTypeId(QVariantMap *) { return QVariant::Map; }

// implement the copy mechanism
template<class T>
struct QDBusTypeHelper
{
    typedef T Type;
    typedef QList<T> List;

    static inline int id()
    {
        Type* t = 0;
        return qDBusMetaTypeId(t);
    }

    static inline int listId()
    {
        List *l = 0;
        return qDBusMetaTypeId(l);
    }
    
    static inline QVariant toVariant(const Type &t)
    {
        return QVariant(id(), &t);
    }

    static bool canSpecialConvert(const QVariant &);
    static Type specialConvert(const QVariant &);

    static inline Type fromVariant(const QVariant &v)
    {
        if (canSpecialConvert(v))
            return specialConvert(v);

        QVariant copy(v);
        if (copy.convert( QVariant::Type(id()) ))
            return *reinterpret_cast<const Type *>(copy.constData());
        return Type();
    }

    static inline QVariantList toVariantList(const List &list)
    {
        QVariantList tmp;
        foreach (const Type &t, list)
            tmp.append(toVariant(t));
        return tmp;
    }

    static inline QVariantList toVariantList(const QVariant &v)
    {
        return toVariantList(QDBusTypeHelper<List>::fromVariant(v));
    }

    static inline List fromVariantList(const QVariantList &list)
    {
        List tmp;
        foreach (const QVariant &v, list)
            tmp.append(fromVariant(v));
        return tmp;
    }
};

template<>
struct QDBusTypeHelper<QVariant>
{
    static inline int id()
    {
        QVariant *t = 0;
        return qDBusMetaTypeId(t);
    }

    static inline int listId()
    {
        return QVariant::List;
    }

    static inline QVariant toVariant(const QVariant &t)
    {
        return QVariant(id(), &t);
    }

    static inline QVariant fromVariant(const QVariant &v)
    {
        if (v.userType() == id())
            return *reinterpret_cast<const QVariant *>(v.constData());
        return v;
    }

    static inline QVariantList toVariantList(const QVariantList &list)
    {
        return list;
    }

    static inline QVariantList fromVariantList(const QVariantList &list)
    {
        return list;
    }
};

#if !defined(QT_NO_CAST_FROM_ASCII) && !defined(QT_NO_CAST_TO_ASCII)
template<>
struct QDBusTypeHelper<char *>
{
    static inline int id()
    { return QVariant::String; }

    static inline QVariant toVariant(const char *t)
    { return QVariant(t); }

    static inline QByteArray fromVariant(const QVariant &v)
    { return v.toString().toAscii(); }
};

template<>
struct QDBusTypeHelper<const char *>
{
    static inline int id()
    { return QVariant::String; }

    static inline QVariant toVariant(const char *t)
    { return QVariant(t); }

    static inline QByteArray fromVariant(const QVariant &v)
    { return v.toString().toAscii(); }
};
#endif

// support three exceptions: uchar, short and ushort
// we have to do this as long as QVariant can't convert to/from the integer metatypes
template<> inline bool QDBusTypeHelper<short>::canSpecialConvert(const QVariant &v)
{ return v.userType() < int(QVariant::UserType); }
template<> inline short QDBusTypeHelper<short>::specialConvert(const QVariant &v)
{ return v.toInt(); }

template<> inline bool QDBusTypeHelper<ushort>::canSpecialConvert(const QVariant &v)
{ return v.userType() < int(QVariant::UserType); }
template<> inline ushort QDBusTypeHelper<ushort>::specialConvert(const QVariant &v)
{ return v.toUInt(); }

template<> inline bool QDBusTypeHelper<uchar>::canSpecialConvert(const QVariant &v)
{ return v.userType() < int(QVariant::UserType); }
template<> inline uchar QDBusTypeHelper<uchar>::specialConvert(const QVariant &v)
{ return v.toUInt(); }

template<typename T> inline bool QDBusTypeHelper<T>::canSpecialConvert(const QVariant &)
{ return false; }
template<typename T> inline T QDBusTypeHelper<T>::specialConvert(const QVariant &)
{ return T(); }

#endif

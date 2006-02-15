/* qdbusmarshall.cpp
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

#include "qdbusmarshall.h"
#include "qdbustype.h"
#include "qdbusvariant.h"

#include <qdebug.h>
#include <qvariant.h>
#include <qlist.h>
#include <qmap.h>
#include <qstringlist.h>
#include <qvarlengtharray.h>
#include <qvector.h>

#include <dbus/dbus.h>

template <typename T>
inline T qIterGet(DBusMessageIter *it)
{
    T t;
    dbus_message_iter_get_basic(it, &t);
    return t;
}

static QStringList qFetchStringList(DBusMessageIter *arrayIt)
{
    QStringList list;

    DBusMessageIter it;
    dbus_message_iter_recurse(arrayIt, &it);

    do {
        list.append(QString::fromUtf8(qIterGet<char *>(&it)));
    } while (dbus_message_iter_next(&it));

    return list;
}

static QVariant qFetchParameter(DBusMessageIter *it)
{
    switch (dbus_message_iter_get_arg_type(it)) {
    case DBUS_TYPE_BYTE:
        return qIterGet<unsigned char>(it);
    case DBUS_TYPE_INT16:
	return qIterGet<dbus_int16_t>(it);
    case DBUS_TYPE_UINT16:
	return qIterGet<dbus_uint16_t>(it);
    case DBUS_TYPE_INT32:
        return qIterGet<dbus_int32_t>(it);
    case DBUS_TYPE_UINT32:
        return qIterGet<dbus_uint32_t>(it);
    case DBUS_TYPE_DOUBLE:
        return qIterGet<double>(it);
    case DBUS_TYPE_BOOLEAN:
        return qIterGet<dbus_bool_t>(it);
    case DBUS_TYPE_INT64:
        return static_cast<qlonglong>(qIterGet<dbus_int64_t>(it));
    case DBUS_TYPE_UINT64:
        return static_cast<qulonglong>(qIterGet<dbus_uint64_t>(it));
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE:
        return QString::fromUtf8(qIterGet<char *>(it));
    case DBUS_TYPE_ARRAY: {
        int arrayType = dbus_message_iter_get_element_type(it);
        if (arrayType == DBUS_TYPE_STRING || arrayType == DBUS_TYPE_OBJECT_PATH ||
            arrayType == DBUS_TYPE_SIGNATURE) {
            return qFetchStringList(it);
        } else if (arrayType == DBUS_TYPE_BYTE) {
            DBusMessageIter sub;
	    dbus_message_iter_recurse(it, &sub);
	    int len = dbus_message_iter_get_array_len(&sub);
	    char* data;
	    dbus_message_iter_get_fixed_array(&sub,&data,&len);
	    return QByteArray(data,len);
        } else if (arrayType == DBUS_TYPE_DICT_ENTRY) {
            // ### support other types of maps?
            QMap<QString, QVariant> map;
            DBusMessageIter sub;
            
            dbus_message_iter_recurse(it, &sub);
            if (!dbus_message_iter_has_next(&sub))
                // empty map
                return map;
            
            do {
                DBusMessageIter itemIter;
                dbus_message_iter_recurse(&sub, &itemIter);
                Q_ASSERT(dbus_message_iter_has_next(&itemIter));
                QString key = qFetchParameter(&itemIter).toString();
                dbus_message_iter_next(&itemIter);
                map.insertMulti(key, qFetchParameter(&itemIter));
            } while (dbus_message_iter_next(&sub));
            return map;
        }
    }
    // fall through
    // common handling for structs and lists
    case DBUS_TYPE_STRUCT: {
        QList<QVariant> list;
        DBusMessageIter sub;
        dbus_message_iter_recurse(it, &sub);
        if (!dbus_message_iter_has_next(&sub))
            return list;
        do {
            list.append(qFetchParameter(&sub));
        } while (dbus_message_iter_next(&sub));
        return list;
    }
    case DBUS_TYPE_VARIANT: {
        QDBusVariant dvariant;
        DBusMessageIter sub;
        dbus_message_iter_recurse(it, &sub);
        dvariant.type = QDBusType(dbus_message_iter_get_signature(&sub));
        dvariant.value = qFetchParameter(&sub);
        return qVariantFromValue(dvariant);
    }

    default:
        qWarning("Don't know how to handle type %d '%c'", dbus_message_iter_get_arg_type(it), dbus_message_iter_get_arg_type(it));
        return QVariant();
        break;
    }
}

void QDBusMarshall::messageToList(QList<QVariant> &list, DBusMessage *message)
{
    Q_ASSERT(message);

    DBusMessageIter it;
    if (!dbus_message_iter_init(message, &it))
        return;

    do {
        list.append(qFetchParameter(&it));
    } while (dbus_message_iter_next(&it));
}

// convert the variant to the given type and return true if it worked.
// if the type is not known, guess it from the variant and set.
// return false if conversion failed.
static bool checkType(QVariant &var, QDBusType &type)
{
    if (!type.isValid()) {
        // guess it from the variant
        type = QDBusType::guessFromVariant(var);
        return true;
    }

    // only catch the conversions that won't work
    // let QVariant do the hard work
    
    // QVariant can't convert QDBusVariant:
    if (var.userType() == qMetaTypeId<QDBusVariant>()) {
        if (type.dbusType() == DBUS_TYPE_VARIANT)
            return true;        // no change

        // convert manually
        QDBusVariant dvariant = qvariant_cast<QDBusVariant>(var);
        var = dvariant.value;
        return checkType(var, type);
    }
    
    if (type.dbusType() == DBUS_TYPE_VARIANT) {
        // variant can handle anything. Let it pass
        return true;
    }

    switch (var.userType()) {
    case QMetaType::Short: 
    case QMetaType::UShort:
    case QMetaType::UChar:
    case QVariant::Int:
    case QVariant::UInt:
    case QVariant::LongLong:
    case QVariant::ULongLong:
    case QVariant::Double:
    case QVariant::String:
        if (type.isBasic())
            // QVariant can handle this on its own
            return true;

        // cannot handle this
        qWarning("Invalid conversion from %s to '%s'", var.typeName(),
                 type.dbusSignature().constData());
        var.clear();
        return false;

    case QVariant::ByteArray:
        // make sure it's an "ARRAY of BYTE"
        if (type.qvariantType() != QVariant::ByteArray) {
            qWarning("Invalid conversion from %s to '%s'", var.typeName(),
                     type.dbusSignature().constData());
            var.clear();
            return false;
        }
        return true;

    case QVariant::StringList:
        // make sure it's "ARRAY of STRING"
        if (type.qvariantType() != QVariant::StringList) {
            qWarning("Invalid conversion from %s to '%s'", var.typeName(),
                     type.dbusSignature().constData());
            var.clear();
            return false;
        }
        return true;

    case QVariant::List:
        // could be either struct or array
        if (type.dbusType() != DBUS_TYPE_ARRAY && type.dbusType() != DBUS_TYPE_STRUCT) {
            qWarning("Invalid conversion from %s to '%s'", var.typeName(),
                     type.dbusSignature().constData());
            var.clear();
            return false;
        }
        
        return true;

    case QVariant::Map:
        if (!type.isMap()) {
            qWarning("Invalid conversion from %s to '%s'", var.typeName(),
                     type.dbusSignature().constData());
            var.clear();
            return false;
        }

        return true;

    case QVariant::Invalid:
        // create an empty variant
        var.convert(type.qvariantType());
        break;        
    }

    qWarning("Found unknown QVariant type %d (%s) when converting to DBus", (int)var.type(),
             var.typeName());
    var.clear();
    return false;
}

static void qVariantToIteratorInternal(DBusMessageIter *it, const QVariant &var,
                                       const QDBusType &type);

static void qListToIterator(DBusMessageIter *it, const QList<QVariant> &list,
                            const QDBusTypeList &list);

template<typename T>
static void qIterAppend(DBusMessageIter *it, const QDBusType &type, T arg)
{
    dbus_message_iter_append_basic(it, type.dbusType(), &arg);
}    

static void qAppendArrayToMessage(DBusMessageIter *it, const QDBusType &subType,
                                  const QVariant &var)
{
    DBusMessageIter sub;
    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, subType.dbusSignature(), &sub);

    switch (var.type())
    {
    case QVariant::StringList: {
        const QStringList list = var.toStringList();
        foreach (QString str, list)
            qIterAppend(&sub, subType, str.toUtf8().constData());
        break;
    }

    case QVariant::ByteArray: {
	const QByteArray array = var.toByteArray();
	const char* cdata = array.constData();
	dbus_message_iter_append_fixed_array(&sub, DBUS_TYPE_BYTE, &cdata, array.length());
        break;
    }

    case QVariant::Map: {
        const QVariantMap map = var.toMap();
        const QDBusTypeList& subTypes = subType.subTypes();
        for (QMap<QString, QVariant>::const_iterator mit = map.constBegin();
             mit != map.constEnd(); ++mit) {
            DBusMessageIter itemIterator;
            dbus_message_iter_open_container(&sub, DBUS_TYPE_DICT_ENTRY, 0, &itemIterator);
            
            // let the string be converted to QVariant
            qVariantToIteratorInternal(&itemIterator, mit.key(), subTypes[0]);
            qVariantToIteratorInternal(&itemIterator, mit.value(), subTypes[1]);
            
            dbus_message_iter_close_container(&sub, &itemIterator);
        }
        break;
    }

    case QVariant::List: {
        const QVariantList list = var.toList();
        foreach (QVariant v, list)
            qVariantToIteratorInternal(&sub, v, subType);
        break;        
    }

    default:
        qFatal("qAppendArrayToMessage got unknown type!");
        break;
    }
    
    dbus_message_iter_close_container(it, &sub);
}

static void qAppendStructToMessage(DBusMessageIter *it, const QDBusTypeList &typeList,
                                   const QVariantList &list)
{
    DBusMessageIter sub;
    dbus_message_iter_open_container(it, DBUS_TYPE_STRUCT, NULL, &sub);
    qListToIterator(&sub, list, typeList);
    dbus_message_iter_close_container(it, &sub);
}

static void qAppendVariantToMessage(DBusMessageIter *it, const QDBusType & /* type */,
                                    const QVariant &var)
{
    QVariant v;
    QDBusType t;
    
    if (var.userType() == qMetaTypeId<QDBusVariant>()) {
        QDBusVariant dvariant = qvariant_cast<QDBusVariant>(var);
        v = dvariant.value;
        t = dvariant.type;
    }
    else {
        v = var;
        t = QDBusType::guessFromVariant(v);
    }
    
    // now add this variant
    DBusMessageIter sub;
    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, t.dbusSignature(), &sub);
    qVariantToIteratorInternal(&sub, v, t);
    dbus_message_iter_close_container(it, &sub);
}

static void qVariantToIterator(DBusMessageIter *it, QVariant var, QDBusType type)
{
    if (var.isNull() && !type.isValid())
        return;                 // cannot add a null like this
    if (!checkType(var, type))
        return;                 // type checking failed

    qVariantToIteratorInternal(it, var, type);
}

static void qVariantToIteratorInternal(DBusMessageIter *it, const QVariant &var,
                                       const QDBusType &type)
{
    switch (type.dbusType()) {
    case DBUS_TYPE_BYTE:
        qIterAppend( it, type, static_cast<unsigned char>(var.toUInt()) );
        break;
    case DBUS_TYPE_BOOLEAN:
        qIterAppend( it, type, static_cast<dbus_bool_t>(var.toBool()) );
        break;
    case DBUS_TYPE_INT16:
        qIterAppend( it, type, static_cast<dbus_int16_t>(var.toInt()) );
        break;
    case DBUS_TYPE_UINT16:
        qIterAppend( it, type, static_cast<dbus_uint16_t>(var.toUInt()) );
        break;
    case DBUS_TYPE_INT32:
        qIterAppend( it, type, static_cast<dbus_int32_t>(var.toInt()) );
        break;
    case DBUS_TYPE_UINT32:
        qIterAppend( it, type, static_cast<dbus_uint32_t>(var.toUInt()) );
        break;
    case DBUS_TYPE_INT64:
        qIterAppend( it, type, static_cast<dbus_int64_t>(var.toLongLong()) );
        break;
    case DBUS_TYPE_UINT64:
        qIterAppend( it, type, static_cast<dbus_uint64_t>(var.toULongLong()) );
        break;
    case DBUS_TYPE_DOUBLE:
        qIterAppend( it, type, var.toDouble() );
        break;
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE:
        qIterAppend( it, type, var.toString().toUtf8().constData() );
        break;

    // compound types:
    case DBUS_TYPE_ARRAY:
        // could be many things
        qAppendArrayToMessage( it, type.arrayElement(), var );
        break;

    case DBUS_TYPE_VARIANT:
        qAppendVariantToMessage( it, type, var );
        break;

    case DBUS_TYPE_STRUCT:
        qAppendStructToMessage( it, type.subTypes(), var.toList() );
        break;

    case DBUS_TYPE_DICT_ENTRY:
        qFatal("qVariantToIterator got a DICT_ENTRY!");
        break;

    default:
        qWarning("Found unknown DBus type '%s'", type.dbusSignature().constData());
        break;
    }
}

void qListToIterator(DBusMessageIter *it, const QList<QVariant> &list)
{
    for (int i = 0; i < list.count(); ++i)
        qVariantToIterator(it, list.at(i), QDBusType());
}

void qListToIterator(DBusMessageIter *it, const QList<QVariant> &list, const QDBusTypeList &types)
{
    int min = qMin(list.count(), types.count());
    for (int i = 0; i < min; ++i)
        qVariantToIterator(it, list.at(i), types.at(i));

    for (int i = min; i < types.count(); ++i)
        // we're missing a few arguments, so add default parameters
        qVariantToIterator(it, QVariant(), types.at(i));
}
    
void QDBusMarshall::listToMessage(const QList<QVariant> &list, DBusMessage *msg,
                                  const QString &signature)
{
    Q_ASSERT(msg);
    DBusMessageIter it;
    dbus_message_iter_init_append(msg, &it);

    if (signature.isEmpty())
        qListToIterator(&it, list);
    else
        qListToIterator(&it, list, QDBusTypeList(signature.toUtf8()));
}


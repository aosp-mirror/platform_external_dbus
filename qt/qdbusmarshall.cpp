/* qdbusmarshall.cpp
 *
 * Copyright (C) 2005 Harald Fernengel <harry@kdevelop.org>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "qdbusmarshall.h"
#include "qdbusvariant.h"

#include <QtCore/qdebug.h>
#include <QtCore/qvariant.h>
#include <QtCore/qlist.h>
#include <QtCore/qmap.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qvarlengtharray.h>
#include <QtCore/qvector.h>

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
    case DBUS_TYPE_INT32:
        return qIterGet<dbus_int32_t>(it);
    case DBUS_TYPE_UINT32:
        return qIterGet<dbus_uint32_t>(it);
    case DBUS_TYPE_DOUBLE:
        return qIterGet<double>(it);
    case DBUS_TYPE_BOOLEAN:
        return qIterGet<dbus_bool_t>(it);
    case DBUS_TYPE_INT64:
        return qIterGet<dbus_int64_t>(it);
    case DBUS_TYPE_UINT64:
        return qIterGet<dbus_uint64_t>(it);
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE:
        return QString::fromUtf8(qIterGet<char *>(it));
    case DBUS_TYPE_ARRAY: {
        int arrayType = dbus_message_iter_get_element_type(it);
        if (arrayType == DBUS_TYPE_STRING || arrayType == DBUS_TYPE_OBJECT_PATH) {
            return qFetchStringList(it);
        } else if (arrayType == DBUS_TYPE_DICT_ENTRY) {
            // ### support other types of maps?
            QMap<QString, QVariant> map;
            DBusMessageIter sub;
            dbus_message_iter_recurse(it, &sub);
            if (!dbus_message_iter_has_next(&sub))
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
        } else {
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
        break; }
    case DBUS_TYPE_VARIANT: {
        QDBusVariant dvariant;
        DBusMessageIter sub;
        dbus_message_iter_recurse(it, &sub);
        dvariant.signature = QString::fromUtf8(dbus_message_iter_get_signature(&sub));
        dvariant.value = qFetchParameter(&sub);
        return qVariantFromValue(dvariant);
    }
#if 0
    case DBUS_TYPE_DICT: {
        QMap<QString, QVariant> map;
        DBusMessageIter sub;
        dbus_message
        if (dbus_message_iter_init_dict_iterator(it, &dictIt)) {
            do {
                map[QString::fromUtf8(dbus_message_iter_get_dict_key(&dictIt))] =
                    qFetchParameter(&dictIt);
            } while (dbus_message_iter_next(&dictIt));
        }
        return map;
        break; }
    case DBUS_TYPE_CUSTOM:
        return qGetCustomValue(it);
        break;
#endif
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

#define DBUS_APPEND(type,dtype,var) \
type dtype##v=(var); \
dbus_message_append_args(msg, dtype, &dtype##v, DBUS_TYPE_INVALID)
#define DBUS_APPEND_LIST(type,dtype,var,size) \
type dtype##v=(var); \
dbus_message_append_args(msg, DBUS_TYPE_ARRAY, dtype, &dtype##v, size, DBUS_TYPE_INVALID)


static void qAppendToMessage(DBusMessageIter *it, const QString &str)
{
    QByteArray ba = str.toUtf8();
    const char *cdata = ba.constData();
    dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &cdata);
}

static QVariant::Type qVariantListType(const QList<QVariant> &list)
{
    // TODO - catch lists that have a list as first parameter
    QVariant::Type tp = list.value(0).type();
    if (tp < QVariant::Int || tp > QVariant::Double)
        return QVariant::Invalid;

    for (int i = 1; i < list.count(); ++i) {
        const QVariant &var = list.at(i);
        if (var.type() != tp
               && (var.type() != QVariant::List || qVariantListType(var.toList()) != tp))
            return QVariant::Invalid;
    }
    return tp;
}

static const char *qDBusListType(const QList<QVariant> &list)
{
    static const char *DBusArgs[] = { 0, 0, DBUS_TYPE_INT32_AS_STRING, DBUS_TYPE_UINT32_AS_STRING,
            DBUS_TYPE_INT64_AS_STRING, DBUS_TYPE_UINT64_AS_STRING, DBUS_TYPE_DOUBLE_AS_STRING };

    return DBusArgs[qVariantListType(list)];
}

static void qListToIterator(DBusMessageIter *it, const QList<QVariant> &list);

static void qVariantToIterator(DBusMessageIter *it, const QVariant &var)
{
    static const int Variant2DBus[] = { DBUS_TYPE_INVALID,
        DBUS_TYPE_BOOLEAN, DBUS_TYPE_INT32, DBUS_TYPE_UINT32,
        DBUS_TYPE_INT64, DBUS_TYPE_UINT64, DBUS_TYPE_DOUBLE };

    // these really are static asserts
    Q_ASSERT(QVariant::Invalid == 0);
    Q_ASSERT(QVariant::Int == 2);
    Q_ASSERT(QVariant::Double == 6);

    switch (var.type()) {
    case QVariant::Int:
    case QVariant::UInt:
    case QVariant::LongLong:
    case QVariant::ULongLong:
    case QVariant::Double:
        dbus_message_iter_append_basic(it, Variant2DBus[var.type()],
                var.constData());
        break;
    case QVariant::String:
        qAppendToMessage(it, var.toString());
        break;
    case QVariant::StringList: {
        const QStringList list = var.toStringList();
        DBusMessageIter sub;
        dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY,
                                         DBUS_TYPE_STRING_AS_STRING, &sub);
        for (int s = 0; s < list.count(); ++s)
            qAppendToMessage(&sub, list.at(s));
        dbus_message_iter_close_container(it, &sub);
        break;
    }
    case QVariant::List: {
        const QList<QVariant> &list = var.toList();
        const char *listType = qDBusListType(list);
        if (!listType) {
            qWarning("Don't know how to marshall list.");
            break;
        }
        DBusMessageIter sub;
        dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, listType, &sub);
        qListToIterator(&sub, list);
        dbus_message_iter_close_container(it, &sub);
        break;
    }
    case QVariant::Map: {
        // ### TODO - marshall more than qstring/qstring maps
        const QMap<QString, QVariant> &map = var.toMap();
        DBusMessageIter sub;
        QVarLengthArray<char, 16> sig;
        sig.append(DBUS_DICT_ENTRY_BEGIN_CHAR);
        sig.append(DBUS_TYPE_STRING);
        sig.append(DBUS_TYPE_STRING);
        sig.append(DBUS_DICT_ENTRY_END_CHAR);
        sig.append('\0');
        qDebug() << QString::fromAscii(sig.constData());
        dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, sig.constData(), &sub);
        for (QMap<QString, QVariant>::const_iterator mit = map.constBegin();
             mit != map.constEnd(); ++mit) {
            DBusMessageIter itemIterator;
            dbus_message_iter_open_container(&sub, DBUS_TYPE_DICT_ENTRY, 0, &itemIterator);
            qAppendToMessage(&itemIterator, mit.key());
            qAppendToMessage(&itemIterator, mit.value().toString());
            dbus_message_iter_close_container(&sub, &itemIterator);
        }
        dbus_message_iter_close_container(it, &sub);
        break;
    }
    case QVariant::UserType: {
        if (var.userType() == QMetaTypeId<QDBusVariant>::qt_metatype_id()) {
            DBusMessageIter sub;
            QDBusVariant dvariant = qvariant_cast<QDBusVariant>(var);
            dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT,
                    dvariant.signature.toUtf8().constData(), &sub);
            qVariantToIterator(&sub, dvariant.value);
            dbus_message_iter_close_container(it, &sub);
            break;
        }
    }
    // fall through
    default:
        qWarning("Don't know how to handle type %s", var.typeName());
        break;
    }
}

void qListToIterator(DBusMessageIter *it, const QList<QVariant> &list)
{
    if (list.isEmpty())
        return;

    for (int i = 0; i < list.count(); ++i)
        qVariantToIterator(it, list.at(i));
}

void QDBusMarshall::listToMessage(const QList<QVariant> &list, DBusMessage *msg)
{
    Q_ASSERT(msg);
    DBusMessageIter it;
    dbus_message_iter_init_append(msg, &it);
    qListToIterator(&it, list);
}


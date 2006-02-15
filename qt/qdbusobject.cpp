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

#include "qdbusmessage.h"
#include "qdbusconnection.h"
#include "qdbusobject.h"
#include "qdbusinterface.h"
#include "qdbusstandardinterfaces.h"
#include "qdbuserror.h"

#include "qdbusxmlparser_p.h"
#include "qdbusobject_p.h"
#include "qdbusutil.h"

QDBusObject::QDBusObject(QDBusObjectPrivate* p, const QDBusConnection& conn)
    :d(p), m_conn(conn)
{
}
    
QDBusObject::QDBusObject(const QDBusConnection& conn, const QString& service, const QString& path)
    : m_conn(conn)
{
    *this = m_conn.findObject(service, path);
}

QDBusObject::QDBusObject(const QDBusInterface& iface)
    : m_conn(iface.connection())
{
    *this = m_conn.findObject(iface.service(), iface.path());
}

QDBusObject::QDBusObject(const QDBusObject& other)
    : d(other.d), m_conn(other.m_conn)
{
}

QDBusObject::~QDBusObject()
{
}

QDBusObject& QDBusObject::operator=(const QDBusObject& other)
{
#if 0    
    if (other.d)
        other.d->ref.ref();
    
    QDBusObjectPrivate* old = qAtomicSetPtr(&d, other.d);
    if (old && !old->ref.deref())
        m_conn.d->disposeOf(d);
#endif
    d = other.d;

    return *this;
}

QDBusConnection QDBusObject::connection() const
{
    return m_conn;
}

QString QDBusObject::service() const
{
    return d ? d->data->service : QString();
}

QString QDBusObject::path() const
{
    return d ? d->data->path : QString();
}

QString QDBusObject::introspect() const
{
    if (!d)
        // not connected
        return QString();

    if (d->data->introspection.isNull()) {
        // Try to introspect
        QDBusIntrospectableInterface iface = *this;
        QString xml = iface.introspect();

        if (!m_conn.lastError().isValid()) {
            // this will change the contents of d->data
            QDBusXmlParser::parse(d, xml);
        }
    }
    return d->data->introspection;
}

QSharedDataPointer<QDBusIntrospection::Object> QDBusObject::introspectionData() const
{
    QSharedDataPointer<QDBusIntrospection::Object> retval;
    if (d)
        retval = const_cast<QDBusIntrospection::Object*>(d->data);
    return retval;
}

QStringList QDBusObject::interfaces() const
{
    introspect();
    return d ? d->data->interfaces : QStringList();
}

QMap<QString, QDBusObject> QDBusObject::children() const
{
    QMap<QString, QDBusObject> retval;
#if 0    
    if (!d)
        return retval;

    QString prefix = d->path;
    if (!prefix.endsWith('/'))
        prefix.append('/');
    foreach (QString sub, d->childObjects)
        retval.insert(sub, QDBusObject( m_conn.d->findObject(d->path, prefix + sub), m_conn ));

    return retval;
#endif
    qFatal("fixme!");
    return retval;
}

bool QDBusObject::isValid() const
{
    return d && m_conn.isConnected() && QDBusUtil::isValidBusName(d->data->service) &&
        QDBusUtil::isValidObjectPath(d->data->path);
}

#if 0                           // we don't have a way of determining if an object exists or not
bool QDBusObject::exists() const
{
    if (!isValid())
        return false;

    // call a non-existant interface/method
    QDBusMessage msg = QDBusMessage::methodCall(d->service, d->path,
                                                "org.freedesktop.DBus.NonExistant", "NonExistant");
    QDBusMessage reply = m_conn.sendWithReply(msg);
    // ignore the reply

    QDBusError err = m_conn.lastError();
    if (!err.isValid()) {
        qWarning("D-Bus call to %s:%s on a supposedly non-existant interface worked!",
                 qPrintable(d->service), qPrintable(d->path));
        return true;
    }

    if (err.name == DBUS_ERROR_SERVICE_UNKNOWN ||
        err.name == DBUS_ERROR_BAD_ADDRESS
    return !m_conn.lastError().isValid();
}
#endif

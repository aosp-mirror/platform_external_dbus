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

#include "qdbusinterface.h"
#include "qdbusobject.h"
#include "qdbusstandardinterfaces.h"

#include "qdbusinterface_p.h"

QDBusInterface::QDBusInterface(QDBusInterfacePrivate* p)
    : d(p)
{
    d->ref.ref();
}

QDBusInterface::QDBusInterface(const QDBusObject& obj, const QString& name)
    : d(0)
{
    *this = obj.connection().findInterface(obj.service(), obj.path(), name);
}

QDBusInterface::QDBusInterface(QDBusConnection& conn, const QString& service, const QString& path,
                               const QString& name)
    : d(0)
{
    *this = conn.findInterface(service, path, name);
}

QDBusInterface::~QDBusInterface()
{
    if (!d->ref.deref())
        delete d;
}

QDBusInterface& QDBusInterface::operator=(const QDBusInterface& other)
{
    other.d->ref.ref();
    QDBusInterfacePrivate* old = qAtomicSetPtr(&d, other.d);
    if (old && !old->ref.deref())
        delete old;

    return *this;
}

QDBusConnection QDBusInterface::connection() const
{
    return d->conn;
}

QString QDBusInterface::service() const
{
    return d->service;
}

QString QDBusInterface::path() const
{
    return d->path;
}

QString QDBusInterface::interface() const
{
    return d->data->name;
}

QString QDBusInterface::introspectionData() const
{
    d->introspect();
    return d->data->introspection;
}

const QDBusIntrospection::Interface& QDBusInterface::interfaceData() const
{
    d->introspect();
    return *d->data;
}

const QDBusIntrospection::Annotations& QDBusInterface::annotationData() const
{
    d->introspect();
    return d->data->annotations;
}

const QDBusIntrospection::Methods& QDBusInterface::methodData() const
{
    d->introspect();
    return d->data->methods;
}

const QDBusIntrospection::Signals& QDBusInterface::signalData() const
{
    d->introspect();
    return d->data->signals_;
}

const QDBusIntrospection::Properties& QDBusInterface::propertyData() const
{
    d->introspect();
    return d->data->properties;
}

QDBusMessage QDBusInterface::callWithArgs(const QDBusIntrospection::Method& method,
                                          const QList<QVariant>& a_args,
                                          CallMode mode)
{
    QString signature("");      // empty, not null
    QVariantList args = a_args;

    if (!method.inputArgs.isEmpty())
    {
        // go over the list of parameters for the method
        QDBusIntrospection::Arguments::const_iterator it = method.inputArgs.begin(),
                                                     end = method.inputArgs.end();
        int arg;
        for (arg = 0; it != end; ++it, ++arg)
        {
            // find the marshalled name for this type
            QString typeSig = QLatin1String(it->type.dbusSignature());
            signature += typeSig;
        }
    }
    else
        args.clear();

    if (method.annotations.value(ANNOTATION_NO_WAIT, "false") == "true")
        mode = NoWaitForReply;

    return callWithArgs(method.name, signature, args, mode);
}

QDBusMessage QDBusInterface::callWithArgs(const QString& method, const QList<QVariant>& args,
                                          CallMode mode)
{
    QString m = method, sig;
    // split out the signature from the method
    int pos = method.indexOf('.');
    if (pos != -1) {
        m.truncate(pos);
        sig = method.mid(pos + 1);
    }    
    return callWithArgs(m, sig, args, mode);
}

QDBusMessage QDBusInterface::callWithArgs(const QString& method, const QString& signature,
                                          const QList<QVariant>& args, CallMode mode)
{
    QDBusMessage msg = QDBusMessage::methodCall(service(), path(), interface(), method, signature);
    msg.QList<QVariant>::operator=(args);

    QDBusMessage reply;
    if (mode == WaitForReply)
        reply = d->conn.sendWithReply(msg);
    else
        d->conn.send(msg);

    d->lastError = reply;       // will clear if reply isn't an error

    // ensure that there is at least one element
    if (reply.isEmpty())
        reply << QVariant();

    return reply;
}

bool QDBusInterface::connect(const QDBusIntrospection::Signal& sig, QObject* obj, const char *slot)
{
    QString signature("");      // empty, not null

    if (!sig.outputArgs.isEmpty())
    {
        // go over the list of parameters for the method
        QDBusIntrospection::Arguments::const_iterator it = sig.outputArgs.begin(),
                                                     end = sig.outputArgs.end();
        int arg;
        for (arg = 0; it != end; ++it, ++arg)
        {
            // find the marshalled name for this type
            QString typeSig = QLatin1String(it->type.dbusSignature());
            signature += typeSig;
        }
    }

    return connect(sig.name, signature, obj, slot);
}

bool QDBusInterface::connect(const QString& signalName, QObject* obj, const char *slot)
{
    QString s = signalName, sig;
    // split out the signature from the name
    int pos = signalName.indexOf('.');
    if (pos != -1) {
        s.truncate(pos);
        sig = signalName.mid(pos + 1);
    }
    return connect(s, sig, obj, slot);
}

bool QDBusInterface::connect(const QString& signalName, const QString& signature,
                             QObject* obj, const char *slot)
{
    return d->conn.connect(service(), path(), interface(), signalName, signature, obj, slot);
}

QVariant QDBusInterface::propertyGet(const QDBusIntrospection::Property& prop)
{
    // sanity checking
    if (prop.access == QDBusIntrospection::Property::Write)
        return QVariant();      // write-only prop

    QDBusPropertiesInterface pi(object());
    return pi.get(interface(), prop.name);
}

QVariant QDBusInterface::propertyGet(const QString& propName)
{
    // can't do sanity checking
    QDBusPropertiesInterface pi(object());
    return pi.get(interface(), propName);
}

void QDBusInterface::propertySet(const QDBusIntrospection::Property& prop, QVariant newValue)
{
    // sanity checking
    if (prop.access == QDBusIntrospection::Property::Read)
        return;

    QDBusPropertiesInterface pi(object());
    pi.set(interface(), prop.name, newValue);
}

void QDBusInterface::propertySet(const QString& propName, QVariant newValue)
{
    // can't do sanity checking
    QDBusPropertiesInterface pi(object());
    pi.set(interface(), propName, newValue);
}

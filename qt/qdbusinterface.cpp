/* -*- C++ -*-
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "qdbusinterface.h"

#include <dbus/dbus.h>
#include <QtCore/qpointer.h>

#include "qdbusinterface_p.h"
#include "qdbusconnection_p.h"

/*!
    \class QDBusInterface
    \brief Proxy class for interfaces on remote objects.

    QDBusInterface is a generic accessor class that is used to place calls to remote objects,
    connect to signals exported by remote objects and get/set the value of remote properties. This
    class is useful for dynamic access to remote objects: that is, when you do not have a generated
    code that represents the remote interface.

    Calls are usually placed by using the call() function, which constructs the message, sends it
    over the bus, waits for the reply and decodes the reply. Signals are connected to by using the
    normal QObject::connect() function. Finally, properties are accessed using the
    QObject::property() and QObject::setProperty() functions. 
*/

QDBusInterface::QDBusInterface(QDBusInterfacePrivate *p)
    : QDBusAbstractInterface(p)
{
}

/*!
    Destroy the object interface and frees up any resource used.
*/
QDBusInterface::~QDBusInterface()
{
    // resources are freed in QDBusInterfacePrivate::~QDBusInterfacePrivate()
}

/*!
    Returns true if this is a valid reference to a remote object. It returns false if
    there was an error during the creation of this interface (for instance, if the remote
    application does not exist).

    Note: when dealing with remote objects, it is not always possible to determine if it
    exists when creating a QDBusInterface or QDBusInterfacePtr object.
*/
bool QDBusInterface::isValid() const
{
    return d_func()->isValid;
}

/*!
    \internal
    Overrides QObject::metaObject to return our own copy.
*/
const QMetaObject *QDBusInterface::metaObject() const
{
    return d_func()->isValid ? d_func()->metaObject : &QDBusAbstractInterface::staticMetaObject;
}

/*!
    \internal
    Override QObject::qt_metacast to catch the interface name too.
*/
void *QDBusInterface::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, "QDBusInterface"))
        return static_cast<void*>(const_cast<QDBusInterface*>(this));
    if (d_func()->interface == _clname)
        return static_cast<void*>(const_cast<QDBusInterface*>(this));
    return QDBusAbstractInterface::qt_metacast(_clname);
}

/*!
    \internal
    Dispatch the call through the private.
*/
int QDBusInterface::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDBusAbstractInterface::qt_metacall(_c, _id, _a);
    if (_id < 0 || !d_func()->isValid)
        return _id;
    return d_func()->metacall(_c, _id, _a);
}

int QDBusInterfacePrivate::metacall(QMetaObject::Call c, int id, void **argv)
{
    Q_Q(QDBusInterface);
    
    if (c == QMetaObject::InvokeMetaMethod) {
        int offset = metaObject->methodOffset();
        QMetaMethod mm = metaObject->method(id + offset);
        
        if (mm.methodType() == QMetaMethod::Signal) {
            // signal relay from D-Bus world to Qt world
            QMetaObject::activate(q, metaObject, id, argv);
            
        } else if (mm.methodType() == QMetaMethod::Slot) {
            // method call relay from Qt world to D-Bus world
            // get D-Bus equivalent signature
            QString methodName = metaObject->dbusNameForMethod(id);
            const int *inputTypes = metaObject->inputTypesForMethod(id);
            const int *outputTypes = metaObject->outputTypesForMethod(id);

            int inputTypesCount = *inputTypes;
            int outputTypesCount = *outputTypes++;

            // we will assume that the input arguments were passed correctly
            QVariantList args;
            for (int i = 1; i <= inputTypesCount; ++i)
                args << QVariant(inputTypes[i], argv[i]);

            // make the call
            QPointer<QDBusInterface> qq = q;
            QDBusMessage reply = q->callWithArgs(methodName, args);
            args.clear();

            // access to "this" or to "q" below this point must check for "qq"
            // we may have been deleted!

            // check if we got the right number of parameters back:
            bool success = false;
            if (reply.count() == outputTypesCount) {
                // copy the values out
                for (int i = 0; i < outputTypesCount; ++i) {
                    // treat the return value specially, since it may be null:
                    if (i == 0 && argv[0] == 0)
                        continue;

                    // ensure that the types are correct:
                    const QVariant &item = reply.at(i);
                    if (outputTypes[i] != item.userType()) {
                        success = false;
                        break;
                    }

                    if (i == 0)
                        QDBusMetaObject::assign(argv[0], item);
                    else
                        QDBusMetaObject::assign(argv[inputTypesCount + i], item);
                }
            }

            // bail out, something weird happened
            if (!success && !qq.isNull()) {
                QString errmsg = QLatin1String("Invalid signature `%1' in return from call to %2.%3");
                lastError = QDBusError(QDBusError::InvalidSignature,
                                       errmsg.arg(reply.signature(), interface, methodName));
            }

            // done
            return -1;
        }
    } else if (c == QMetaObject::ReadProperty) {
        // Qt doesn't support non-readable properties
        // we have to re-check
        QMetaProperty mp = metaObject->property(id + metaObject->propertyOffset());
        if (!mp.isReadable())
            return -1;          // don't read

        // try to read this property
        QDBusMessage msg = QDBusMessage::methodCall(service, path, DBUS_INTERFACE_PROPERTIES,
                                                    QLatin1String("Get"));
        msg << interface << QString::fromUtf8(mp.name());

        QPointer<QDBusAbstractInterface> qq = q;
        QDBusMessage reply = connp->sendWithReply(msg, QDBusConnection::UseEventLoop);

        // access to "this" or to "q" below this point must check for "qq"
        // we may have been deleted!

        if (reply.type() == QDBusMessage::ReplyMessage && reply.count() == 1 &&
            reply.signature() == QLatin1String("v")) {
            QVariant value = QDBusTypeHelper<QVariant>::fromVariant(reply.at(0));

            // make sure the type is right
            if (strcmp(mp.typeName(), value.typeName()) == 0) {
                if (mp.type() == QVariant::LastType)
                    // QVariant is special in this context
                    *reinterpret_cast<QVariant *>(argv[0]) = value;
                else
                    QDBusMetaObject::assign(argv[0], value);
                return -1;
            }
        }

        // got an error
        if (qq.isNull())
            return -1;          // bail out

        if (reply.type() == QDBusMessage::ErrorMessage)
            lastError = reply;
        else if (reply.signature() != QLatin1String("v")) {
            QString errmsg = QLatin1String("Invalid signature `%1' in return from call to "
                                           DBUS_INTERFACE_PROPERTIES);
            lastError = QDBusError(QDBusError::InvalidSignature, errmsg.arg(reply.signature()));
        } else {
            QString errmsg = QLatin1String("Unexpected type `%1' when retrieving property "
                                           "`%2 %3.%4'");
            lastError = QDBusError(QDBusError::InvalidSignature,
                                   errmsg.arg(QLatin1String(reply.at(0).typeName()),
                                              QLatin1String(mp.typeName()),
                                              interface, QString::fromUtf8(mp.name())));
        }
        return -1;
    } else if (c == QMetaObject::WriteProperty) {
        // QMetaProperty::write has already checked that we're writable
        // it has also checked that the type is right
        QVariant value(metaObject->propertyMetaType(id), argv[0]);
        QMetaProperty mp = metaObject->property(id + metaObject->propertyOffset());

        // send the value
        QDBusMessage msg = QDBusMessage::methodCall(service, path, DBUS_INTERFACE_PROPERTIES,
                                                    QLatin1String("Set"));
        msg.setSignature(QLatin1String("ssv"));
        msg << interface << QString::fromUtf8(mp.name()) << value;

        QPointer<QDBusAbstractInterface> qq = q;
        QDBusMessage reply = connp->sendWithReply(msg, QDBusConnection::UseEventLoop);

        // access to "this" or to "q" below this point must check for "qq"
        // we may have been deleted!

        if (!qq.isNull() && reply.type() != QDBusMessage::ReplyMessage)
            lastError = reply;

        return -1;
    }
    return id;
}

QDBusInterfacePtr::QDBusInterfacePtr(QDBusConnection &conn, const QString &service, const QString &path,
                   const QString &interface)
    : d(conn.findInterface(service, path, interface))
{
}

QDBusInterfacePtr::QDBusInterfacePtr(const QString &service, const QString &path, const QString &interface)
    : d(QDBus::sessionBus().findInterface(service, path, interface))
{
}


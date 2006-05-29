/* -*- mode: C++ -*-
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

#include "qdbusabstractadaptor.h"

#include <QtCore/qmetaobject.h>
#include <QtCore/qtimer.h>

#include "qdbusconnection.h"

#include "qdbusconnection_p.h"  // for qDBusParametersForMethod
#include "qdbusabstractadaptor_p.h"

struct QDBusAdaptorInit
{
    QSignalSpyCallbackSet callbacks;
    QDBusAdaptorInit()
    {
        extern void qt_register_signal_spy_callbacks(const QSignalSpyCallbackSet &callback_set);
        callbacks.signal_begin_callback = QDBusAdaptorConnector::signalBeginCallback;
        callbacks.signal_end_callback = QDBusAdaptorConnector::signalEndCallback;
        callbacks.slot_begin_callback = 0;
        callbacks.slot_end_callback = 0;
        qt_register_signal_spy_callbacks(callbacks);
        
        //QDBusAdaptorConnector::id = QObject::registerUserData();
    }
};

Q_GLOBAL_STATIC(QDBusAdaptorInit, qAdaptorInit)

QDBusAdaptorConnector *qDBusFindAdaptorConnector(QObject *obj)
{
    (void)qAdaptorInit();

    if (!obj)
        return 0;
    QDBusAdaptorConnector *connector = qFindChild<QDBusAdaptorConnector *>(obj);
    if (connector)
        connector->polish();
    return connector;
}

QDBusAdaptorConnector *qDBusFindAdaptorConnector(QDBusAbstractAdaptor *adaptor)
{
    return qDBusFindAdaptorConnector(adaptor->parent());
}

QDBusAdaptorConnector *qDBusCreateAdaptorConnector(QObject *obj)
{
    (void)qAdaptorInit();

    QDBusAdaptorConnector *connector = qDBusFindAdaptorConnector(obj);
    if (connector)
        return connector;
    return new QDBusAdaptorConnector(obj);
}

QString QDBusAbstractAdaptorPrivate::retrieveIntrospectionXml(QDBusAbstractAdaptor *adaptor)
{
    return adaptor->d->xml;
}

void QDBusAbstractAdaptorPrivate::saveIntrospectionXml(QDBusAbstractAdaptor *adaptor,
                                                       const QString &xml)
{
    adaptor->d->xml = xml;
}

/*!
    \page usingannotations.html
    \title Using annotations in adaptors

    It is currently not possible to specify arbitrary annotations in adaptors.
*/

/*!
    \class QDBusAbstractAdaptor
    \brief Abstract adaptor for D-Bus adaptor classes.

    The QDBusAbstractAdaptor class is the starting point for all objects intending to provide
    interfaces to the external world using D-Bus. This is accomplished by attaching a one or more
    classes derived from QDBusAbstractAdaptor to a normal QObject and then registering that QObject
    with QDBusConnection::registerObject. QDBusAbstractAdaptor objects are intended to be
    light-weight wrappers, mostly just relaying calls into the real object (see object()) and the
    signals from it.

    Each QDBusAbstractAdaptor-derived class should define the D-Bus interface it is implementing
    using the Q_CLASSINFO macro in the class definition.

    QDBusAbstractAdaptor uses the standard QObject mechanism of signals, slots and properties to
    determine what signals, methods and properties to export to the bus. Any signal emitted by
    QDBusAbstractAdaptor-derived classes will be automatically be relayed through any D-Bus
    connections the object is registered on.

    Classes derived from QDBusAbstractAdaptor must be created on the heap using the \a new operator
    and must not be deleted by the user (they will be deleted automatically when the object they are
    connected to is also deleted).

    \sa {usingadaptors.html}{Using adaptors}, QDBusConnection
*/

/*!
    Constructs a QDBusAbstractAdaptor with \a parent as the object we refer to.
*/
QDBusAbstractAdaptor::QDBusAbstractAdaptor(QObject* parent)
    : QObject(parent), d(new QDBusAbstractAdaptorPrivate)
{
    QDBusAdaptorConnector *connector = qDBusCreateAdaptorConnector(parent);

    connector->waitingForPolish = true;
    QTimer::singleShot(0, connector, SLOT(polish()));
}

/*!
    Destroys the adaptor.

    \warning Adaptors are destroyed automatically when the real object they refer to is
             destroyed. Do not delete the adaptors yourself.
*/
QDBusAbstractAdaptor::~QDBusAbstractAdaptor()
{
    delete d;
}

/*!
    Returns the QObject that we're the adaptor for. This is the same object that was passed as an
    argument to the QDBusAbstractAdaptor constructor.
*/
QObject* QDBusAbstractAdaptor::object() const
{
    return parent();
}

/*!
    Toggles automatic signal relaying from the real object (see object()).

    Automatic signal relaying consists of signal-to-signal connection of the signals on the parent
    that have the exact same method signatue in both classes.

    If \a enable is set to true, connect the signals; if set to false, disconnect all signals.
*/
void QDBusAbstractAdaptor::setAutoRelaySignals(bool enable)
{
    const QMetaObject *us = metaObject();
    const QMetaObject *them = parent()->metaObject();
    for (int idx = staticMetaObject.methodCount(); idx < us->methodCount(); ++idx) {
        QMetaMethod mm = us->method(idx);

        if (mm.methodType() != QMetaMethod::Signal)
            continue;
        
        // try to connect/disconnect to a signal on the parent that has the same method signature
        QByteArray sig = QMetaObject::normalizedSignature(mm.signature());
        if (them->indexOfSignal(sig) == -1)
            continue;
        sig.prepend(QSIGNAL_CODE + '0');
        parent()->disconnect(sig, this, sig);
        if (enable)
            connect(parent(), sig, sig);
    }
}

QDBusAdaptorConnector::QDBusAdaptorConnector(QObject *parent)
    : QObject(parent), waitingForPolish(false), lastSignalIdx(0), argv(0)
{
}

QDBusAdaptorConnector::~QDBusAdaptorConnector()
{
}

void QDBusAdaptorConnector::addAdaptor(QDBusAbstractAdaptor *adaptor)
{
    // find the interface name
    const QMetaObject *mo = adaptor->metaObject();
    while (mo != &QDBusAbstractAdaptor::staticMetaObject) {
        int ciend = mo->classInfoCount();
        for (int i = mo->classInfoOffset(); i < ciend; ++i) {
            QMetaClassInfo mci = mo->classInfo(i);
            if (strcmp(mci.name(), QCLASSINFO_DBUS_INTERFACE) == 0 && *mci.value()) {
                // find out if this interface exists first
                QString interface = QString::fromUtf8(mci.value());
                AdaptorMap::Iterator it = qLowerBound(adaptors.begin(), adaptors.end(), interface);
                if (it != adaptors.end() && it->interface == interface) {
                    // exists. Replace it (though it's probably the same)
                    it->adaptor = adaptor;
                    it->metaObject = mo;
                } else {
                    // create a new one
                    AdaptorData entry;
                    entry.interface = interface;
                    entry.adaptor = adaptor;
                    entry.metaObject = mo;
                    adaptors << entry;
                }
            }
        }

        mo = mo->superClass();
    }
        
    // connect the adaptor's signals to our relaySlot slot
    mo = adaptor->metaObject();
    for (int i = QDBusAbstractAdaptor::staticMetaObject.methodCount();
         i < mo->methodCount(); ++i) {
        QMetaMethod mm = mo->method(i);

        if (mm.methodType() != QMetaMethod::Signal)
            continue;

        QByteArray sig = mm.signature();
        sig.prepend(QSIGNAL_CODE + '0');
        disconnect(adaptor, sig, this, SLOT(relaySlot()));
        connect(adaptor, sig, this, SLOT(relaySlot()));
    }
}

void QDBusAdaptorConnector::polish()
{
    if (!waitingForPolish)
        return;                 // avoid working multiple times if multiple adaptors were added

    waitingForPolish = false;
    const QObjectList &objs = parent()->children();
    foreach (QObject *obj, objs) {
        QDBusAbstractAdaptor *adaptor = qobject_cast<QDBusAbstractAdaptor *>(obj);
        if (adaptor)
            addAdaptor(adaptor);
    }

    // sort the adaptor list
    qSort(adaptors);
}

void QDBusAdaptorConnector::relaySlot()
{
    relay(sender());
}

void QDBusAdaptorConnector::relay(QObject *sender)
{
    // we're being called because there is a signal being emitted that we must relay
    Q_ASSERT(lastSignalIdx);
    Q_ASSERT(argv);
    Q_ASSERT(senderMetaObject);

    if (senderMetaObject != sender->metaObject()) {
        qWarning("Inconsistency detected: QDBusAdaptorConnector::relay got called with unexpected sender object!");
    } else {
        QMetaMethod mm = senderMetaObject->method(lastSignalIdx);
        QObject *object = static_cast<QDBusAbstractAdaptor *>(sender)->parent();

        // break down the parameter list
        QList<int> types;
        int inputCount = qDBusParametersForMethod(mm, types);
        if (inputCount == -1)
            // invalid signal signature
            // qDBusParametersForMethod has already complained
            return;
        if (inputCount + 1 != types.count() ||
            types.at(inputCount) == QDBusConnectionPrivate::messageMetaType) {
            // invalid signal signature
            // qDBusParametersForMethod has not yet complained about this one
            qWarning("Cannot relay signal %s::%s", senderMetaObject->className(), mm.signature());
            return;
        }

        QByteArray signature = QMetaObject::normalizedSignature(mm.signature());
        signature.truncate(signature.indexOf('(')); // remove parameter decoration

        QVariantList args;
        for (int i = 1; i < types.count(); ++i)
            args << QVariant(types.at(i), argv[i]);

        // find all the interfaces this signal belongs to
        for (const QMetaObject *mo = senderMetaObject; mo != &QDBusAbstractAdaptor::staticMetaObject;
             mo = mo->superClass()) {
            if (lastSignalIdx < mo->methodOffset())
                break;

            for (int i = mo->classInfoOffset(); i < mo->classInfoCount(); ++i) {
                QMetaClassInfo mci = mo->classInfo(i);
                if (qstrcmp(mci.name(), QCLASSINFO_DBUS_INTERFACE) == 0 && *mci.value())
                    // now emit the signal with all the information
                    emit relaySignal(object, mci.value(), signature.constData(), args);
            }
        }
    }
}

void QDBusAdaptorConnector::signalBeginCallback(QObject *caller, int method_index, void **argv)
{
    QDBusAbstractAdaptor *adaptor = qobject_cast<QDBusAbstractAdaptor *>(caller);
    if (adaptor) {
        QDBusAdaptorConnector *data = qDBusFindAdaptorConnector(adaptor);
        data->lastSignalIdx = method_index;
        data->argv = argv;
        data->senderMetaObject = caller->metaObject();
        data->polish();         // make sure it's polished
    }
}

void QDBusAdaptorConnector::signalEndCallback(QObject *caller, int)
{
    QDBusAbstractAdaptor *adaptor = qobject_cast<QDBusAbstractAdaptor *>(caller);
    if (adaptor) {
        QDBusAdaptorConnector *data = qDBusFindAdaptorConnector(adaptor);
        data->lastSignalIdx = 0;
        data->argv = 0;
        data->senderMetaObject = 0;
    }
}

#include "qdbusabstractadaptor.moc"
#include "qdbusabstractadaptor_p.moc"

/* -*- mode: C++; set-fill-width: 100 -*-
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

#ifndef QDBUSABSTRACTADAPTORPRIVATE_H
#define QDBUSABSTRACTADAPTORPRIVATE_H

#include <QtCore/qobject.h>
#include <QtCore/qmap.h>
#include <QtCore/qhash.h>
#include <QtCore/qreadwritelock.h>
#include <QtCore/qvariant.h>
#include <QtCore/qvector.h>

#define QCLASSINFO_DBUS_INTERFACE       "D-Bus Interface"
#define QCLASSINFO_DBUS_INTROSPECTION   "D-Bus Introspection"

class QDBusAbstractAdaptor;
class QDBusAdaptorConnector;
class QDBusAdaptorManager;
class QDBusConnectionPrivate;

#if QT_VERSION < 0x040200
/* mirrored in qobject_p.h, DON'T CHANGE without prior warning */
struct QSignalSpyCallbackSet
{
    typedef void (*BeginCallback)(QObject *caller, int method_index, void **argv);
    typedef void (*EndCallback)(QObject *caller, int method_index);
    BeginCallback signal_begin_callback,
                    slot_begin_callback;
    EndCallback signal_end_callback,
                slot_end_callback;
};
#else
# error Qt 4.2.0 is supposed to have a better solution!
  CHOKE!
#endif  // Qt 4.2.0

class QDBusAbstractAdaptorPrivate
{
public:
    QString xml;

    static QString retrieveIntrospectionXml(QDBusAbstractAdaptor *adaptor);
    static void saveIntrospectionXml(QDBusAbstractAdaptor *adaptor, const QString &xml);
};

class QDBusAdaptorConnector: public QObject
{
    Q_OBJECT
public: // typedefs
    struct AdaptorData
    {
        QString interface;
        QDBusAbstractAdaptor *adaptor;
        const QMetaObject *metaObject;

        inline bool operator<(const AdaptorData &other) const
        { return interface < other.interface; }
        inline bool operator<(const QString &other) const
        { return interface < other; }
    };
    typedef QVector<AdaptorData> AdaptorMap;

public: // methods
    explicit QDBusAdaptorConnector(QObject *parent);
    ~QDBusAdaptorConnector();

    void addAdaptor(QDBusAbstractAdaptor *adaptor);
    void relay(QObject *sender);

public slots:
    void relaySlot();
    void polish();

signals:
    void relaySignal(QObject *obj, const char *interface, const char *name, const QVariantList &args);

public: // member variables
    AdaptorMap adaptors;
    bool waitingForPolish : 1;

    int lastSignalIdx;
    void **argv;
    const QMetaObject *senderMetaObject;

public: // static members
    static void signalBeginCallback(QObject *caller, int method_index, void **argv);
    static void signalEndCallback(QObject *caller, int method_index);
    //static int id;
};

extern QDBusAdaptorConnector *qDBusFindAdaptorConnector(QObject *object);
extern QDBusAdaptorConnector *qDBusCreateAdaptorConnector(QObject *object);

#endif // QDBUSABSTRACTADAPTORPRIVATE_H

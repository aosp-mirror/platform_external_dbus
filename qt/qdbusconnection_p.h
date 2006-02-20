/* qdbusconnection_p.h QDBusConnection private object
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

#ifndef QDBUSCONNECTION_P_H
#define QDBUSCONNECTION_P_H

#include "qdbuserror.h"

#include <QtCore/qatomic.h>
#include <QtCore/qmutex.h>
#include <QtCore/qhash.h>
#include <QtCore/qobject.h>
#include <QtCore/qpointer.h>
#include <QtCore/qvarlengtharray.h>
#include <QtCore/qeventloop.h>
#include <QtCore/qmutex.h>

#include <dbus/dbus.h>

#include "qdbusmessage.h"
#include "qdbusintrospection.h"

class QDBusMessage;
class QSocketNotifier;
class QTimerEvent;
class QDBusObjectPrivate;
class CallDeliveryEvent;

typedef struct DBusConnection;
typedef struct DBusServer;

class QDBusConnectionPrivate: public QObject
{
    Q_OBJECT
public:
    // structs and enums
    enum ConnectionMode { InvalidMode, ServerMode, ClientMode };

    struct Watcher
    {
        Watcher(): watch(0), read(0), write(0) {}
        DBusWatch *watch;
        QSocketNotifier *read;
        QSocketNotifier *write;
    };

    struct SignalHook
    {
        QString interface, name, signature;
        QPointer<QObject> obj;
        int midx;
        QList<int> params;
    };

    struct ObjectData
    {
        QPointer<QObject> obj;
        int flags;
    };

public:
    // typedefs
    typedef QMultiHash<int, Watcher> WatcherHash;
    typedef QHash<int, DBusTimeout *> TimeoutHash;
    typedef QMultiHash<QString, SignalHook> SignalHookHash;
    typedef QHash<QString, ObjectData> ObjectDataHash;
    typedef QHash<QString, ObjectDataHash> ObjectHookHash;
    typedef QHash<QString, QSharedDataPointer<QDBusIntrospection::Interface> > KnownInterfacesHash;
    typedef QHash<QString, QDBusIntrospection::Object* > KnownObjectsHash;

public:
    // public methods
    QDBusConnectionPrivate(QObject *parent = 0);
    ~QDBusConnectionPrivate();

    void bindToApplication();

    void setConnection(DBusConnection *connection);
    void setServer(DBusServer *server);
    void closeConnection();
    void timerEvent(QTimerEvent *e);

    bool handleSignal(const QString &path, const QDBusMessage &msg);
    bool send(const QDBusMessage &message) const;
    int sendWithReplyAsync(const QDBusMessage &message, QObject *receiver,
                           const char *method) const;
    
    bool handleSignal(const QDBusMessage &msg);
    bool handleObjectCall(const QDBusMessage &message);
    bool handleError();

    void disposeOfLocked(QDBusIntrospection::Object* obj);
    void disposeOf(QDBusObjectPrivate* obj);
    QSharedDataPointer<QDBusIntrospection::Interface> findInterface(const QString& name);
    QDBusIntrospection::Object* findObject(const QString& service,
                                           const QString& path);
                                                                                  
    bool activateReply(QObject *object, int idx, const QList<int>& metaTypes,
                       const QDBusMessage &msg);
    bool activateSignal(const SignalHook& hook, const QDBusMessage &msg);
    bool activateCall(QObject* object, int flags, const QDBusMessage &msg);
    bool activateAdaptor(QObject *object, int flags, const QDBusMessage &msg);
    bool activateObject(const ObjectData& data, const QDBusMessage &msg);
    void deliverCall(const CallDeliveryEvent &data) const;

protected:
    virtual void customEvent(QEvent *event);

public slots:
    // public slots
    void socketRead(int);
    void socketWrite(int);
    void objectDestroyed(QObject *o);

public:
    // public member variables
    DBusError error;
    QDBusError lastError;

    QAtomic ref;
    QMutex mutex;
    ConnectionMode mode;
    DBusConnection *connection;
    DBusServer *server;

    WatcherHash watchers;
    TimeoutHash timeouts;
    SignalHookHash signalHooks;
    ObjectHookHash objectHooks;
    QList<DBusTimeout *> pendingTimeouts;

public:
    // public mutable member variables
    mutable KnownInterfacesHash knownInterfaces;
    mutable KnownObjectsHash knownObjects;

public:
    // static methods
    static int messageMetaType;
    static int registerMessageMetaType();
    static int findSlot(QObject *obj, const char *slotName, QList<int>& params);
};

class QDBusReplyWaiter: public QEventLoop
{
    Q_OBJECT
public:
    QDBusMessage replyMsg;

public slots:
    void reply(const QDBusMessage &msg);
};    

#endif

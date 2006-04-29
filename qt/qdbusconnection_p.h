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
#include <QtCore/qeventloop.h>
#include <QtCore/qhash.h>
#include <QtCore/qmutex.h>
#include <QtCore/qobject.h>
#include <QtCore/qpointer.h>
#include <QtCore/qreadwritelock.h>
#include <QtCore/qvarlengtharray.h>
#include <QtCore/qvector.h>

#include <dbus/dbus.h>

#include "qdbusmessage.h"

class QDBusMessage;
class QSocketNotifier;
class QTimerEvent;
class QDBusObjectPrivate;
class CallDeliveryEvent;
class QMetaMethod;
class QDBusInterfacePrivate;
class QDBusMetaObject;
class QDBusAbstractInterface;
class QDBusBusService;

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
        inline SignalHook() : obj(0), midx(-1) { }
        QString interface, name, signature;
        QObject* obj;
        int midx;
        QList<int> params;
    };

    struct ObjectTreeNode
    {
        struct Data
        {
            QString name;
            ObjectTreeNode *node;

            inline bool operator<(const QString &other) const
            { return name < other; }
        };

        inline ObjectTreeNode() : obj(0), flags(0) { }
        inline ~ObjectTreeNode() { clear(); }
        inline void clear()
        {
            foreach (const Data &entry, children) {
                entry.node->clear();
                delete entry.node;
            }
            children.clear();
        }

        QObject* obj;
        int flags;
        QVector<Data> children;
    };

public:
    // typedefs
    typedef QMultiHash<int, Watcher> WatcherHash;
    typedef QHash<int, DBusTimeout *> TimeoutHash;
    typedef QMultiHash<QString, SignalHook> SignalHookHash;
    typedef QHash<QString, QDBusMetaObject* > MetaObjectHash;
    
public:
    // public methods
    QDBusConnectionPrivate(QObject *parent = 0);
    ~QDBusConnectionPrivate();

    void bindToApplication();

    void setConnection(DBusConnection *connection);
    void setServer(DBusServer *server);
    void closeConnection();
    void timerEvent(QTimerEvent *e);

    QString getNameOwner(const QString &service);    

    bool send(const QDBusMessage &message) const;
    QDBusMessage sendWithReply(const QDBusMessage &message, int mode);
    int sendWithReplyAsync(const QDBusMessage &message, QObject *receiver,
                           const char *method);
    void connectSignal(const QString &key, const SignalHook &hook);
    void registerObject(const ObjectTreeNode *node);
    void connectRelay(const QString &service, const QString &path, const QString &interface,
                      QDBusAbstractInterface *receiver, const char *signal);
    void disconnectRelay(const QString &service, const QString &path, const QString &interface,
                         QDBusAbstractInterface *receiver, const char *signal);
    
    bool handleSignal(const QString &path, const QDBusMessage &msg);
    bool handleSignal(const QDBusMessage &msg);
    bool handleObjectCall(const QDBusMessage &message);
    bool handleError();

    bool activateSignal(const SignalHook& hook, const QDBusMessage &msg);
    bool activateCall(QObject* object, int flags, const QDBusMessage &msg);
    bool activateObject(const ObjectTreeNode *node, const QDBusMessage &msg);
    bool activateInternalFilters(const ObjectTreeNode *node, const QDBusMessage &msg);

    void postCallDeliveryEvent(CallDeliveryEvent *data);
    CallDeliveryEvent *postedCallDeliveryEvent();
    void deliverCall(const CallDeliveryEvent &data) const;

    QDBusInterfacePrivate *findInterface(const QString &service, const QString &path,
                                         const QString &interface);

protected:
    virtual void customEvent(QEvent *event);

private:
    QDBusMetaObject *findMetaObject(const QString &service, const QString &path,
                                    const QString &interface);        

public slots:
    // public slots
    void socketRead(int);
    void socketWrite(int);
    void objectDestroyed(QObject *o);
    void relaySignal(QObject *obj, const char *interface, const char *name, const QVariantList &args);

public:
    // public member variables
    QString name;               // this connection's name
    
    DBusError error;
    QDBusError lastError;

    QAtomic ref;
    QReadWriteLock lock;
    ConnectionMode mode;
    DBusConnection *connection;
    DBusServer *server;
    QDBusBusService *busService;

    WatcherHash watchers;
    TimeoutHash timeouts;
    SignalHookHash signalHooks;
    QList<DBusTimeout *> pendingTimeouts;

    ObjectTreeNode rootNode;
    MetaObjectHash cachedMetaObjects;

    QMutex callDeliveryMutex;
    CallDeliveryEvent *callDeliveryState; // protected by the callDeliveryMutex mutex

public:
    // static methods
    static int messageMetaType;
    static int registerMessageMetaType();
    static int findSlot(QObject *obj, const char *slotName, QList<int>& params);
    static DBusHandlerResult messageFilter(DBusConnection *, DBusMessage *, void *);
    static void messageResultReceived(DBusPendingCall *, void *);
};

class QDBusReplyWaiter: public QEventLoop
{
    Q_OBJECT
public:
    QDBusMessage replyMsg;

public slots:
    void reply(const QDBusMessage &msg);
};

extern int qDBusParametersForMethod(const QMetaMethod &mm, QList<int>& metaTypes);
extern int qDBusNameToTypeId(const char *name);
extern bool qDBusCheckAsyncTag(const char *tag);

// in qdbusinternalfilters.cpp
extern QString qDBusIntrospectObject(const QDBusConnectionPrivate::ObjectTreeNode *node);
extern void qDBusIntrospectObject(const QDBusConnectionPrivate::ObjectTreeNode *node,
                                  const QDBusMessage &msg);
extern void qDBusPropertyGet(const QDBusConnectionPrivate::ObjectTreeNode *node,
                             const QDBusMessage &msg);
extern void qDBusPropertySet(const QDBusConnectionPrivate::ObjectTreeNode *node,
                             const QDBusMessage &msg);

#endif

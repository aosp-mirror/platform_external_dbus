/* qdbusconnection_p.h QDBusConnection private object
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
#include <QtCore/qhash.h>
#include <QtCore/qobject.h>
#include <QtCore/qpointer.h>
#include <QtCore/qvarlengtharray.h>

#include <dbus/dbus.h>

class QDBusMessage;
class QSocketNotifier;
class QTimerEvent;

typedef struct DBusConnection;
typedef struct DBusServer;

class QDBusConnectionPrivate: public QObject
{
    Q_OBJECT
public:
    QDBusConnectionPrivate(QObject *parent = 0);
    ~QDBusConnectionPrivate();

    void bindToApplication();

    void setConnection(DBusConnection *connection);
    void setServer(DBusServer *server);
    void closeConnection();
    void timerEvent(QTimerEvent *e);

    bool handleSignal(DBusMessage *msg) const;
    bool handleObjectCall(DBusMessage *message) const;
    bool handleError();

public slots:
    void socketRead(int);
    void socketWrite(int);
    void objectDestroyed(QObject *o);

public:
    DBusError error;
    QDBusError lastError;

    enum ConnectionMode { InvalidMode, ServerMode, ClientMode };

    QAtomic ref;
    ConnectionMode mode;
    DBusConnection *connection;
    DBusServer *server;

    static int messageMetaType;
    static int registerMessageMetaType();
    bool handleSignal(const QString &path, const QDBusMessage &msg) const;
    int sendWithReplyAsync(const QDBusMessage &message, QObject *receiver,
                           const char *method) const;

    struct Watcher
    {
        Watcher(): watch(0), read(0), write(0) {}
        DBusWatch *watch;
        QSocketNotifier *read;
        QSocketNotifier *write;
    };
    typedef QMultiHash<int, Watcher> WatcherHash;
    WatcherHash watchers;

    typedef QHash<int, DBusTimeout *> TimeoutHash;
    TimeoutHash timeouts;

    struct SignalHook
    {
        QString interface, name;
        QPointer<QObject> obj;
        int midx;
        QVarLengthArray<int, 10> params;

        bool setSlot(const char *slotName);
    };

    typedef QMultiHash<QString, SignalHook> SignalHookHash;
    SignalHookHash signalHooks;

    struct ObjectHook
    {
        QString interface;
        QPointer<QObject> obj;
    };
    typedef QMultiHash<QString, ObjectHook> ObjectHookHash;
    ObjectHookHash objectHooks;
    QList<DBusTimeout *> pendingTimeouts;
};

#endif

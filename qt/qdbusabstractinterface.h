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

#ifndef QDBUSABSTRACTINTERFACE_H
#define QDBUSABSTRACTINTERFACE_H

#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
#include <QtCore/qlist.h>
#include <QtCore/qobject.h>

#include "qdbusmessage.h"
#include "qdbustypehelper_p.h"

class QDBusConnection;
class QDBusError;

class QDBusAbstractInterfacePrivate;
class QDBUS_EXPORT QDBusAbstractInterface: public QObject
{
    Q_OBJECT

public:
    enum CallMode {
        NoWaitForReply,
        UseEventLoop,
        NoUseEventLoop,
        AutoDetect
    };

public:
    virtual ~QDBusAbstractInterface();

    QDBusConnection connection() const;

    QString service() const;
    QString path() const;
    QString interface() const;

    QDBusError lastError() const;

    QDBusMessage callWithArgs(const QString &method, const QList<QVariant> &args = QList<QVariant>(),
                              CallMode mode = AutoDetect);
    bool callWithArgs(const QString &method, QObject *receiver, const char *slot,
                      const QList<QVariant> &args = QList<QVariant>());

    inline QDBusMessage call(const QString &m)
    {
        return callWithArgs(m);
    }

    inline QDBusMessage call(CallMode mode, const QString &m)
    {
        return callWithArgs(m, QList<QVariant>(), mode);
    }
    
#ifndef Q_QDOC
private:
    template<typename T> inline QVariant qvfv(const T &t)
    { return QDBusTypeHelper<T>::toVariant(t); }
    
public:
    template<typename T1>
    inline QDBusMessage call(const QString &m, const T1 &t1)
    {
        QList<QVariant> args;
        args << qvfv(t1);
        return callWithArgs(m, args);
    }

    template<typename T1, typename T2>
    inline QDBusMessage call(const QString &m, const T1 &t1, const T2 &t2)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2);
        return callWithArgs(m, args);
    }

    template<typename T1, typename T2, typename T3>
    inline QDBusMessage call(const QString &m, const T1 &t1, const T2 &t2, const T3 &t3)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2) << qvfv(t3);
        return callWithArgs(m, args);
    }
      
    template<typename T1, typename T2, typename T3, typename T4>
    inline QDBusMessage call(const QString &m, const T1 &t1, const T2 &t2, const T3 &t3,
                             const T4 &t4)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2) << qvfv(t3)
             << qvfv(t4);
        return callWithArgs(m, args);
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5>
    inline QDBusMessage call(const QString &m, const T1 &t1, const T2 &t2, const T3 &t3,
                             const T4 &t4, const T5 &t5)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2) << qvfv(t3)
             << qvfv(t4) << qvfv(t5);
        return callWithArgs(m, args);
    }
  
    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
    inline QDBusMessage call(const QString &m, const T1 &t1, const T2 &t2, const T3 &t3,
                             const T4 &t4, const T5 &t5, const T6 &t6)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2) << qvfv(t3)
             << qvfv(t4) << qvfv(t5) << qvfv(t6);
        return callWithArgs(m, args);
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
    inline QDBusMessage call(const QString &m, const T1 &t1, const T2 &t2, const T3 &t3,
                             const T4 &t4, const T5 &t5, const T6 &t6, const T7 &t7)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2) << qvfv(t3)
             << qvfv(t4) << qvfv(t5) << qvfv(t6)
             << qvfv(t7);
        return callWithArgs(m, args);
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5,
             typename T6, typename T7, typename T8>
    inline QDBusMessage call(const QString &m, const T1 &t1, const T2 &t2, const T3 &t3,
                             const T4 &t4, const T5 &t5, const T6 &t6, const T7 &t7, const T8 &t8)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2) << qvfv(t3)
             << qvfv(t4) << qvfv(t5) << qvfv(t6)
             << qvfv(t7) << qvfv(t8);
        return callWithArgs(m, args);
    }

    template<typename T1>
    inline QDBusMessage call(CallMode mode, const QString &m, const T1 &t1)
    {
        QList<QVariant> args;
        args << qvfv(t1);
        return callWithArgs(m, args, mode);
    }

    template<typename T1, typename T2>
    inline QDBusMessage call(CallMode mode, const QString &m, const T1 &t1, const T2 &t2)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2);
        return callWithArgs(m, args, mode);
    }

    template<typename T1, typename T2, typename T3>
    inline QDBusMessage call(CallMode mode, const QString &m, const T1 &t1, const T2 &t2,
                             const T3 &t3)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2) << qvfv(t3);
        return callWithArgs(m, args, mode);
    }
      
    template<typename T1, typename T2, typename T3, typename T4>
    inline QDBusMessage call(CallMode mode, const QString &m, const T1 &t1, const T2 &t2,
                             const T3 &t3, const T4 &t4)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2) << qvfv(t3)
             << qvfv(t4);
        return callWithArgs(m, args, mode);
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5>
    inline QDBusMessage call(CallMode mode, const QString &m, const T1 &t1, const T2 &t2,
                             const T3 &t3, const T4 &t4, const T5 &t5)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2) << qvfv(t3)
             << qvfv(t4) << qvfv(t5);
        return callWithArgs(m, args, mode);
    }
  
    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
    inline QDBusMessage call(CallMode mode, const QString &m, const T1 &t1, const T2 &t2,
                             const T3 &t3, const T4 &t4, const T5 &t5, const T6 &t6)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2) << qvfv(t3)
             << qvfv(t4) << qvfv(t5) << qvfv(t6);
        return callWithArgs(m, args, mode);
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
    inline QDBusMessage call(CallMode mode, const QString &m, const T1 &t1, const T2 &t2,
                             const T3 &t3, const T4 &t4, const T5 &t5, const T6 &t6, const T7 &t7)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2) << qvfv(t3)
             << qvfv(t4) << qvfv(t5) << qvfv(t6)
             << qvfv(t7);
        return callWithArgs(m, args, mode);
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5,
             typename T6, typename T7, typename T8>
    inline QDBusMessage call(CallMode mode, const QString &m, const T1 &t1, const T2 &t2,
                             const T3 &t3, const T4 &t4, const T5 &t5, const T6 &t6, const T7 &t7,
                             const T8 &t8)
    {
        QList<QVariant> args;
        args << qvfv(t1) << qvfv(t2) << qvfv(t3)
             << qvfv(t4) << qvfv(t5) << qvfv(t6)
             << qvfv(t7) << qvfv(t8);
        return callWithArgs(m, args, mode);
    }
#endif

protected:
    QDBusAbstractInterface(QDBusAbstractInterfacePrivate *);
    void connectNotify(const char *signal);
    void disconnectNotify(const char *signal);
    QVariant internalPropGet(const char *propname) const;
    void internalPropSet(const char *propname, const QVariant &value);

private:
    friend class QDBusInterface;
    QDBusAbstractInterfacePrivate *d_ptr; // remove for Qt 4.2.0

    Q_DECLARE_PRIVATE(QDBusAbstractInterface)
    Q_DISABLE_COPY(QDBusAbstractInterface)
};

#endif

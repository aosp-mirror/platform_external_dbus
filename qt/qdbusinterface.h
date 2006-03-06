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

#ifndef QDBUSINTERFACE_H
#define QDBUSINTERFACE_H

#include "qdbusmessage.h"
#include "qdbusobject.h"
#include "qdbusintrospection.h"
#include "qdbusreply.h"
#include "qdbusvariant.h"
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
#include <QtCore/qlist.h>

class QDBusConnection;

class QDBusInterfacePrivate;
class QDBUS_EXPORT QDBusInterface
{
    friend class QDBusConnection;

public:
    enum CallMode {
        WaitForReply,
        NoWaitForReply
    };

public:
    QDBusInterface(const QDBusObject& obj, const QString& name);
    QDBusInterface(const QDBusInterface&);
    virtual ~QDBusInterface();

    QDBusInterface& operator=(const QDBusInterface&);
    
    inline QDBusObject object()
    { return QDBusObject(*this); }

    inline const QDBusObject object() const
    { return QDBusObject(*this); }

    inline operator QDBusObject()
    { return QDBusObject(*this); }

    inline operator const QDBusObject() const
    { return QDBusObject(*this); }

    
    QDBusConnection connection() const;

    QString service() const;
    QString path() const;
    QString interface() const;

    virtual QString introspectionData() const;
    const QDBusIntrospection::Interface& interfaceData() const;
    const QDBusIntrospection::Annotations& annotationData() const;
    const QDBusIntrospection::Methods& methodData() const;
    const QDBusIntrospection::Signals& signalData() const;
    const QDBusIntrospection::Properties& propertyData() const;

    QDBusMessage callWithArgs(const QDBusIntrospection::Method& method,
                              const QList<QVariant>& args = QList<QVariant>(),
                              CallMode mode = WaitForReply);
    QDBusMessage callWithArgs(const QString& method, const QList<QVariant>& args = QList<QVariant>(),
                              CallMode mode = WaitForReply);
    QDBusMessage callWithArgs(const QString& method, const QString& signature,
                              const QList<QVariant>& args = QList<QVariant>(),
                              CallMode mode = WaitForReply);
   
    bool connect(const QDBusIntrospection::Signal&, QObject* obj, const char *slot);
    bool connect(const QString& signalName, QObject* obj, const char *slot);
    bool connect(const QString& signalName, const QString& signature,
                 QObject* obj, const char *slot);

    QDBusReply<QDBusVariant> property(const QDBusIntrospection::Property&);
    QDBusReply<QDBusVariant> property(const QString& property);

    QDBusReply<void> setProperty(const QDBusIntrospection::Property&, const QDBusVariant& newValue);
    QDBusReply<void> setProperty(const QString& property, const QDBusVariant& newValue);

#ifndef DOXYGEN_SHOULD_SKIP_THIS
    template<typename MethodType>
    inline QDBusMessage call(MethodType m)
    {
        return callWithArgs(m);
    }

    template<typename MethodType, typename T1>
        inline QDBusMessage call(MethodType m, T1 t1)
    {
        QList<QVariant> args;
        args << t1;
        return callWithArgs(m, args);
    }

    template<typename MethodType, typename T1, typename T2>
        inline QDBusMessage call(MethodType m, T1 t1, T2 t2)
    {
        QList<QVariant> args;
        args << t1 << t2;
        return callWithArgs(m, args);
    }

    template<typename MethodType, typename T1, typename T2, typename T3>
        inline QDBusMessage call(MethodType m, T1 t1, T2 t2, T3 t3)
    {
        QList<QVariant> args;
        args << t1 << t2 << t3;
        return callWithArgs(m, args);
    }
      
    template<typename MethodType, typename T1, typename T2, typename T3, typename T4>
        inline QDBusMessage call(MethodType m, T1 t1, T2 t2, T3 t3, T4 t4)
    {
        QList<QVariant> args;
        args << t1 << t2 << t3 << t4;
        return callWithArgs(m, args);
    }

    template<typename MethodType, typename T1, typename T2, typename T3, typename T4, typename T5>
        inline QDBusMessage call(MethodType m, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5)
    {
        QList<QVariant> args;
        args << t1 << t2 << t3 << t4 << t5;
        return callWithArgs(m, args);
    }
  
    template<typename MethodType, typename T1, typename T2, typename T3, typename T4, typename T5,
        typename T6>
        inline QDBusMessage call(MethodType m, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6)
    {
        QList<QVariant> args;
        args << t1 << t2 << t3 << t4 << t5 << t6;
        return callWithArgs(m, args);
    }

    template<typename MethodType, typename T1, typename T2, typename T3, typename T4, typename T5,
        typename T6, typename T7>
        inline QDBusMessage call(MethodType m, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7)
    {
        QList<QVariant> args;
        args << t1 << t2 << t3 << t4 << t5 << t6 << t7;
        return callWithArgs(m, args);
    }

    template<typename MethodType, typename T1, typename T2, typename T3, typename T4, typename T5,
        typename T6, typename T7, typename T8>
        inline QDBusMessage call(MethodType m, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7, T8 t8)
    {
        QList<QVariant> args;
        args << t1 << t2 << t3 << t4 << t5 << t6 << t7 << t8;
        return callWithArgs(m, args);
    }
#else
    // fool Doxygen
    inline QDBusMessage call(const QDBusIntrospection::Method &method, ...);
    inline QDBusMessage call(const QString &method, ...);
#endif

private:
    QDBusInterface(QDBusInterfacePrivate*);
    QDBusInterfacePrivate *d;
};

#endif

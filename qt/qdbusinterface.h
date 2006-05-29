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

#ifndef QDBUSINTERFACE_H
#define QDBUSINTERFACE_H

#include "qdbusabstractinterface.h"

class QDBusInterfacePrivate;
class QDBUS_EXPORT QDBusInterface: public QDBusAbstractInterface
{
    friend class QDBusConnection;
private:
    QDBusInterface(QDBusInterfacePrivate *p);
    
public:
    ~QDBusInterface();
    
    virtual const QMetaObject *metaObject() const;
    virtual void *qt_metacast(const char *);
    virtual int qt_metacall(QMetaObject::Call, int, void **);

private:
    Q_DECLARE_PRIVATE(QDBusInterface);
    Q_DISABLE_COPY(QDBusInterface)
};

struct QDBUS_EXPORT QDBusInterfacePtr
{
    QDBusInterfacePtr(QDBusInterface *iface) : d(iface) { }
    QDBusInterfacePtr(QDBusConnection &conn, const QString &service, const QString &path,
             const QString &interface = QString());
    QDBusInterfacePtr(const QString &service, const QString &path, const QString &interface = QString());
    ~QDBusInterfacePtr() { delete d; }

    QDBusInterface *interface() { return d; }
    QDBusInterface *operator->() { return d; }
private:
    QDBusInterface *const d;
    Q_DISABLE_COPY(QDBusInterfacePtr)
};

#endif

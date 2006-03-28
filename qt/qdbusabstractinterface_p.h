/* 
 *
 * Copyright (C) 2006 Thiago José Macieira <thiago@kde.org>
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

#ifndef QDBUSABSTRACTINTERFACEPRIVATE_H
#define QDBUSABSTRACTINTERFACEPRIVATE_H

#include "qdbusabstractinterface.h"
#include "qdbusconnection.h"
#include "qdbuserror.h"

#define ANNOTATION_NO_WAIT      "org.freedesktop.DBus.Method.NoReply"

class QDBusAbstractInterfacePrivate//: public QObjectPrivate
{
public:
    Q_DECLARE_PUBLIC(QDBusAbstractInterface)
    
    QDBusAbstractInterface *q_ptr; // remove in Qt 4.2
    QDBusConnection conn;
    QDBusConnectionPrivate *connp;
    QString service;
    QString path;
    QString interface;
    QDBusError lastError;

    inline QDBusAbstractInterfacePrivate(const QDBusConnection& con, QDBusConnectionPrivate *conp,
                                         const QString &serv, const QString &p, const QString &iface)
        : conn(con), connp(conp), service(serv), path(p), interface(iface)
    { }
    virtual ~QDBusAbstractInterfacePrivate() { }
};


#endif

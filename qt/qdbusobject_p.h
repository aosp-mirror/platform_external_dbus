/* 
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

#ifndef QDBUSOBJECTPRIVATE_H
#define QDBUSOBJECTPRIVATE_H

#include "QtCore/qatomic.h"
#include "QtCore/qstringlist.h"
#include "qdbusobject.h"
#include "qdbusinterface.h"
#include "qdbusconnection_p.h"

class QDBusObject;
class QDBusInterface;
class QDBusXmlParser;

class QDBusObjectPrivate: public QSharedData
{
public:
    inline QDBusObjectPrivate(QDBusConnectionPrivate* ptr, const QString &service,
                              const QString &path)
        : parent(ptr),
          data(  )
    {
        QDBusIntrospection::Object * d = ptr->findObject(service, path);
        d->ref.ref();
        data = d;
    }

    inline ~QDBusObjectPrivate()
    { parent->disposeOf(this); }

    QDBusConnectionPrivate* parent;
    const QDBusIntrospection::Object* data;
};    

#endif

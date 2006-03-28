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

#ifndef QDBUSMETAOBJECTPRIVATE_H
#define QDBUSMETAOBJECTPRIVATE_H

#include <QtCore/qmetaobject.h>
#include "qdbusmacros.h"

class QDBusError;

class QDBusMetaObjectPrivate;
struct QDBUS_EXPORT QDBusMetaObject: public QMetaObject
{
    bool cached;

    static QDBusMetaObject *createMetaObject(QString &interface, const QString &xml,
                                             QHash<QString, QDBusMetaObject *> &map,
                                             QDBusError &error);
    ~QDBusMetaObject()
    {
        delete [] d.stringdata;
        delete [] d.data;
    }

    // methods (slots & signals):
    const char *dbusNameForMethod(int id) const;
    const char *inputSignatureForMethod(int id) const;
    const char *outputSignatureForMethod(int id) const;
    const int *inputTypesForMethod(int id) const;
    const int *outputTypesForMethod(int id) const;

    // properties:
    int propertyMetaType(int id) const;

    // helper function:
    static void assign(void *, const QVariant &value);

private:
    QDBusMetaObject();
};

struct QDBusMetaTypeId
{
    static bool innerInitialize();
    static bool initialized;
    static inline void initialize()
    {
        if (initialized) return;
        innerInitialize();
    }
    
    static int variant;
    static int boollist;
    static int shortlist;
    static int ushortlist;
    static int intlist;
    static int uintlist;
    static int longlonglist;
    static int ulonglonglist;
    static int doublelist;
};

#endif

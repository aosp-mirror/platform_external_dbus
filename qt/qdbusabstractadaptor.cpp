/* -*- mode: C++ -*-
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

#include "qdbusabstractadaptor.h"

#include <QtCore/qmetaobject.h>
#include <QtCore/qtimer.h>

QDBusAbstractAdaptor::QDBusAbstractAdaptor(QObject* parent)
    : QObject(parent)
{
    QTimer::singleShot(0, this, SLOT(polish()));
}

QDBusAbstractAdaptor::~QDBusAbstractAdaptor()
{
}

void QDBusAbstractAdaptor::setAutoRelaySignals(bool enable)
{
    const QMetaObject *us = metaObject();
    for (int idx = staticMetaObject.methodCount(); idx < us->methodCount(); ++idx) {
        QMetaMethod mm = us->method(idx);

        if (mm.methodType() != QMetaMethod::Signal)
            continue;
        
        // try to connect/disconnect to a signal on the parent that has the same method signature
        QByteArray sig = mm.signature();
        sig.prepend(QSIGNAL_CODE + '0');
        if (enable)
            connect(parent(), sig, sig);
        else
            parent()->disconnect(sig, this, sig);
    }
}

void QDBusAbstractAdaptor::polish()
{
    // future work:
    //  connect every signal in this adaptor to a slot that will relay them into D-Bus
}

#include "qdbusabstractadaptor.moc"

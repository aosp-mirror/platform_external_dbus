/* qdbusserver.h QDBusServer object
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

#ifndef QDBUSSERVER_H
#define QDBUSSERVER_H

#include "dbus/qdbus.h"
#include <QtCore/qobject.h>
#include <QtCore/qstring.h>

class QDBusConnectionPrivate;
class QDBusError;

class QDBUS_EXPORT QDBusServer: public QObject
{
    Q_OBJECT
public:
    QDBusServer(const QString &address, QObject *parent = 0);

    bool isConnected() const;
    QDBusError lastError() const;
    QString address() const;

private:
    Q_DISABLE_COPY(QDBusServer)
    QDBusConnectionPrivate *d;
};

#endif

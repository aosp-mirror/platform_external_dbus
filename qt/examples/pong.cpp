/* -*- C++ -*-
 *
 * Copyright (C) 2006 Trolltech AS. All rights reserved.
 *    Author: Thiago Macieira <thiago.macieira@trolltech.com>
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

#include <stdio.h>

#include <dbus/qdbus.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>

#include "ping-common.h"
#include "pong.h"

QString Pong::ping(const QString &arg)
{
    QTimer::singleShot(0, QCoreApplication::instance(), SLOT(quit()));
    return QString("ping(\"%1\") got called").arg(arg);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QDBusBusService *bus = QDBus::sessionBus().busService();
    if (bus->requestName(SERVICE_NAME, QDBusBusService::AllowReplacingName) !=
        QDBusBusService::PrimaryOwnerReply)
        exit(1);

    Pong pong;
    QDBus::sessionBus().registerObject("/", &pong, QDBusConnection::ExportSlots);
    
    app.exec();
    return 0;
}

#include "pong.moc"

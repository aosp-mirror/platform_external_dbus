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

#include "ping-common.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QDBusInterface *iface = QDBus::sessionBus().findInterface(SERVICE_NAME, "/");
    if (iface) {
        QDBusReply<QString> reply = iface->call("ping", argc > 1 ? argv[1] : "");
        if (reply.isSuccess()) {
            printf("Reply was: %s\n", qPrintable(reply.value()));
            return 0;
        }

        fprintf(stderr, "Call failed: %s\n", qPrintable(reply.error().message()));
        return 1;
    }

    fprintf(stderr, "%s\n",
            qPrintable(QDBus::sessionBus().lastError().message()));
    return 1;
}

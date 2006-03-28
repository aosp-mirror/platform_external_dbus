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
#include <QtCore/QFile>
#include <QtCore/QDebug>
#include <QtCore/QProcess>

#include "ping-common.h"
#include "complexping.h"

void Ping::start(const QString &name, const QString &oldValue, const QString &newValue)
{
    Q_UNUSED(oldValue);

    if (name != SERVICE_NAME || newValue.isEmpty())
        return;

    // open stdin for reading
    qstdin.open(stdin, QIODevice::ReadOnly);

    // find our remote
    iface = QDBus::sessionBus().findInterface(SERVICE_NAME, "/");
    if (!iface) {
        fprintf(stderr, "%s\n",
                qPrintable(QDBus::sessionBus().lastError().message()));
        QCoreApplication::instance()->quit();
    }

    connect(iface, SIGNAL(aboutToQuit()), QCoreApplication::instance(), SLOT(quit()));

    while (true) {
        qDebug() << "Ready";

        QString line = QString::fromLocal8Bit(qstdin.readLine()).trimmed();
        if (line.isEmpty()) {
            iface->call("quit");
            return;
        } else if (line == "value") {
            QVariant reply = iface->property("value");
            if (!reply.isNull())
                qDebug() << "value =" << reply.toString();
        } else if (line.startsWith("value=")) {
            iface->setProperty("value", line.mid(6));            
        } else {
            QDBusReply<QVariant> reply = iface->call("query", line);
            if (reply.isSuccess())
                qDebug() << "Reply was:" << reply.value();
        }

        if (iface->lastError().isValid())
            fprintf(stderr, "Call failed: %s\n", qPrintable(iface->lastError().message()));
    }
}    

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Ping ping;
    ping.connect(QDBus::sessionBus().busService(),
                 SIGNAL(nameOwnerChanged(QString,QString,QString)),
                 SLOT(start(QString,QString,QString)));

    QProcess pong;
    pong.start("./complexpong");

    app.exec();
}

#include "complexping.moc"

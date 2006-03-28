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
#include "complexpong.h"

// the property
QString Pong::value() const
{
    return m_value;
}

void Pong::setValue(const QString &newValue)
{
    m_value = newValue;
}

void Pong::quit()
{
    QTimer::singleShot(0, QCoreApplication::instance(), SLOT(quit()));
}

QVariant Pong::query(const QString &query)
{
    QString q = query.toLower();
    if (q == "hello")
        return "World";
    if (q == "ping")
        return "Pong";
    if (q == "the answer to life, the universe and everything")
        return 42;
    if (q.indexOf("unladen swallow") != -1) {
        if (q.indexOf("european") != -1)
            return 11.0;
        return QByteArray("african or european?");
    }

    return "Sorry, I don't know the answer";
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QDBusBusService *bus = QDBus::sessionBus().busService();

    QObject obj;
    Pong *pong = new Pong(&obj);
    pong->connect(&app, SIGNAL(aboutToQuit()), SIGNAL(aboutToQuit()));
    pong->setProperty("value", "initial value");
    QDBus::sessionBus().registerObject("/", &obj);

    if (bus->requestName(SERVICE_NAME, QDBusBusService::AllowReplacingName) !=
        QDBusBusService::PrimaryOwnerReply)
        exit(1);
    
    app.exec();
    return 0;
}

#include "complexpong.moc"

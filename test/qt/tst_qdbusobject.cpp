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
#include <qcoreapplication.h>
#include <qmetatype.h>
#include <QtTest/QtTest>

#include <dbus/qdbus.h>

const char introspectionData[] =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>"

    "<interface name=\"org.freedesktop.DBus.Introspectable\">"
    "<method name=\"Introspect\">"
    "<arg name=\"data\" direction=\"out\" type=\"s\"/>"
    "</method>"
    "</interface>"

    "<interface name=\"com.trolltech.tst_qdbusobject.MyObject\">"
    "<method name=\"ping\">"
    "<arg name=\"ping\" direction=\"in\"  type=\"v\"/>"
    "<arg name=\"pong\" direction=\"out\" type=\"v\"/>"
    "</method>"
    "</interface>"
    "<node name=\"subObject\"/>"
    "</node>";

class MyObject: public QObject
{
    Q_OBJECT
public slots:

    void ping(const QDBusMessage &msg)
    {
        QDBusConnection con = QDBusConnection::addConnection(QDBusConnection::SessionBus);
        QDBusMessage reply = QDBusMessage::methodReply(msg);
        reply << static_cast<QList<QVariant> >(msg);
        if (!con.send(reply))
            exit(1);
    }

    void Introspect(const QDBusMessage &msg)
    {
        QDBusConnection con = QDBusConnection::addConnection(QDBusConnection::SessionBus);
        QDBusMessage reply = QDBusMessage::methodReply(msg);
        reply << ::introspectionData;
        if (!con.send(reply))
            exit(1);
    }
};

class tst_QDBusObject: public QObject
{
    Q_OBJECT
    MyObject obj;

private slots:
    void initTestCase();        // connect to D-Bus
    void cleanupTestCase();     // disconnect from D-Bus

    void construction_data();
    void construction();

    void introspection_data();
    void introspection();
};

void tst_QDBusObject::initTestCase()
{
    QDBusConnection con = QDBusConnection::addConnection(QDBusConnection::SessionBus);
    QVERIFY(con.isConnected());
    QVERIFY(con.requestName("com.trolltech.tst_qdbusobject"));

    con.registerObject("/", "org.freedesktop.DBus.Introspectable", &obj);
    con.registerObject("/", "com.trolltech.tst_qdbusobject.MyObject", &obj);
}

void tst_QDBusObject::cleanupTestCase()
{
    QDBusConnection::closeConnection();
    QVERIFY(!QDBusConnection().isConnected());
}

void tst_QDBusObject::construction_data()
{
    QTest::addColumn<QString>("service");
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("isValid");
    QTest::addColumn<bool>("exists");

    QTest::newRow("null") << QString() << QString() << false << false;

    QTest::newRow("invalid1") << "foo.foo1" << "" << false << false;
    QTest::newRow("invalid2") << "foo.foo1" << "foo.bar" << false << false;
    QTest::newRow("invalid3") << "foo.foo1" << "/foo.bar" << false << false;
    QTest::newRow("invalid4") << "" << "/" << false << false;
    QTest::newRow("invalid5") << "foo" << "/" << false << false;
    QTest::newRow("invalid6") << ".foo" << "/" << false << false;

    QTest::newRow("invalid7") << "org.freedesktop.DBus" << "" << false << false;
    QTest::newRow("invalid8") << "org.freedesktop.DBus" << "foo.bar" << false << false;
    QTest::newRow("invalid9") << "org.freedesktop.DBus" << "/foo.bar" << false << false;
    
    QTest::newRow("existing") << "org.freedesktop.DBus" << "/" << true << true;
    QTest::newRow("non-existing") << "org.freedesktop.DBus" << "/foo" << true << false;
}

void tst_QDBusObject::construction()
{
    QDBusConnection con;        // default

    QFETCH(QString, service);
    QFETCH(QString, path);
    QFETCH(bool, isValid);
    //QFETCH(bool, exists);

    QDBusObject o(con, service, path);
    QCOMPARE(o.isValid(), isValid);

    if (isValid) {
        QCOMPARE(o.service(), service);
        QCOMPARE(o.path(), path);
    }
    else {
        QVERIFY(o.service().isNull());
        QVERIFY(o.path().isNull());
    }
   
    //QCOMPARE(o.exists(), exists);
}

void tst_QDBusObject::introspection_data()
{
    QTest::addColumn<QString>("service");
    QTest::addColumn<QString>("path");
    QTest::addColumn<QStringList>("interfaces");

    QStringList interfaces;
    QTest::newRow("nowhere") << QString() << QString() << interfaces;

    // IMPORTANT!
    // Keep the interface list sorted!
    interfaces << "org.freedesktop.DBus" << DBUS_INTERFACE_INTROSPECTABLE;
    QTest::newRow("server") << "org.freedesktop.DBus" << "/" << interfaces;

    QDBusConnection con;
    interfaces.clear();
    interfaces << "com.trolltech.tst_qdbusobject.MyObject" << DBUS_INTERFACE_INTROSPECTABLE;    

    QTest::newRow("self1") << con.baseService() << "/" << interfaces;
    QTest::newRow("self2") << "com.trolltech.tst_qdbusobject" << "/" << interfaces;
}

void tst_QDBusObject::introspection()
{
    QDBusConnection con;

    QFETCH(QString, service);
    QFETCH(QString, path);

    QDBusObject o(con, service, path);

    if (!o.isValid())
        QVERIFY(o.introspect().isEmpty());
    else {
        QFETCH(QStringList, interfaces);
        QStringList parsed = o.interfaces();
        parsed.sort();
        QCOMPARE(parsed.count(), interfaces.count());
        QCOMPARE(parsed, interfaces);
    }
}

QTEST_MAIN(tst_QDBusObject)

#include "tst_qdbusobject.moc"


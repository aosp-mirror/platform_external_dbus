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
#include <QtCore/qvariant.h>

Q_DECLARE_METATYPE(QVariantList)

#define TEST_INTERFACE_NAME "com.trolltech.QtDBus.MyObject"
#define TEST_SERVICE_NAME "com.trolltech.QtDBus.tst_qdbusinterface"
#define TEST_SIGNAL_NAME "somethingHappened"

const char introspectionData[] =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>"

    "<interface name=\"org.freedesktop.DBus.Introspectable\">"
    "<method name=\"Introspect\">"
    "<arg name=\"data\" direction=\"out\" type=\"s\"/>"
    "</method>"
    "</interface>"

    "<interface name=\"" TEST_INTERFACE_NAME "\">"
    "<method name=\"ping\">"
    "<arg name=\"ping\" direction=\"in\"  type=\"v\"/>"
    "<arg name=\"pong\" direction=\"out\" type=\"v\"/>"
    "</method>"
    "<method name=\"ping\">"
    "<arg name=\"ping1\" direction=\"in\"  type=\"v\"/>"
    "<arg name=\"ping2\" direction=\"in\"  type=\"v\"/>"
    "<arg name=\"pong1\" direction=\"out\" type=\"v\"/>"
    "<arg name=\"pong2\" direction=\"out\" type=\"v\"/>"
    "</method>"
    "<signal name=\"" TEST_SIGNAL_NAME "\">"
    "<arg type=\"s\"/>"
    "</signal>"
    "<property name=\"prop1\" access=\"readwrite\" type=\"i\" />"
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

class Spy: public QObject
{
    Q_OBJECT
public:
    QString received;
    int count;

    Spy() : count(0)
    { }

public slots:
    void spySlot(const QString& arg)
    {
        received = arg;
        ++count;
    }
};

// helper function
void emitSignal(const QString &interface, const QString &name, const QString &arg)
{
    QDBusMessage msg = QDBusMessage::signal("/", interface, name);
    msg << arg;
    QDBusConnection().send(msg);

    QTest::qWait(200);
}

class tst_QDBusInterface: public QObject
{
    Q_OBJECT
    MyObject obj;
private slots:
    void initTestCase();
    void cleanupTestCase();

    void call_data();
    void call();

    void introspect_data();
    void introspect();

    void signal();
};

void tst_QDBusInterface::initTestCase()
{
    QDBusConnection con = QDBusConnection::addConnection(QDBusConnection::SessionBus);
    QVERIFY(con.isConnected());
    QVERIFY(con.requestName( TEST_SERVICE_NAME ));

    con.registerObject("/", "org.freedesktop.DBus.Introspectable", &obj);
    con.registerObject("/", TEST_INTERFACE_NAME, &obj);
}

void tst_QDBusInterface::cleanupTestCase()
{
    QDBusConnection::closeConnection();
    QVERIFY(!QDBusConnection().isConnected());
}

void tst_QDBusInterface::call_data()
{
    QTest::addColumn<QString>("method");
    QTest::addColumn<QVariantList>("input");
    QTest::addColumn<QVariantList>("output");

    QVariantList input;
    QTest::newRow("empty") << "ping" << input << input;

    input << qVariantFromValue(1);
    QTest::newRow("int") << "ping" << input << input;
    QTest::newRow("int-int") << "ping.i" << input << input;
    QTest::newRow("int-int16") << "ping.n" << input << input;

    // try doing some conversions
    QVariantList output;
    output << qVariantFromValue(1U);
    QTest::newRow("int-uint") << "ping.u" << input << output;
    QTest::newRow("int-uint16") << "ping.q" << input << output;

    QTest::newRow("int-int64") << "ping.x" << input << (QVariantList() << qVariantFromValue(1LL));
    QTest::newRow("int-uint64") << "ping.t" << input << (QVariantList() << qVariantFromValue(1ULL));
    QTest::newRow("int-double") << "ping.d" << input << (QVariantList() << qVariantFromValue(1.0));

    output.clear();
    output << QString("1");
    QTest::newRow("int-string") << "ping.s" << input << output;

    // try from string now
    input = output;
    QTest::newRow("string") << "ping" << input << output;
    QTest::newRow("string-string") << "ping.s" << input << output;

    output.clear();
    output << qVariantFromValue(1);
    QTest::newRow("string-int") << "ping.i" << input << input;
    QTest::newRow("string-int16") << "ping.n" << input << input;

    output.clear();
    output << qVariantFromValue(1U);
    QTest::newRow("string-uint") << "ping.u" << input << output;
    QTest::newRow("string-uint16") << "ping.q" << input << output;

    QTest::newRow("string-int64") << "ping.x" << input << (QVariantList() << qVariantFromValue(1LL));
    QTest::newRow("string-uint64") << "ping.t" << input << (QVariantList() << qVariantFromValue(1ULL));
    QTest::newRow("string-double") << "ping.d" << input << (QVariantList() << qVariantFromValue(1.0));

    // two args (must be strings!)
    input.clear();
    input << QString("Hello") << QString("World");
    output = input;
    QTest::newRow("two-strings") << "ping" << input << output;
    QTest::newRow("two-strings") << "ping.ss" << input << output;

    // this should drop one of the arguments
    output.removeLast();
    QTest::newRow("last-dropped") << "ping.s" << input << output;
}

void tst_QDBusInterface::call()
{
    QDBusConnection con;
    QDBusInterface iface(con, con.baseService(), QLatin1String("/"),
                         TEST_INTERFACE_NAME);

    QFETCH(QString, method);
    QFETCH(QVariantList, input);
    QFETCH(QVariantList, output);
    
    QDBusMessage reply;
    // try first callWithArgs:
    reply = iface.callWithArgs(method, input);

    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    if (!output.isEmpty()) {
        QCOMPARE(reply.count(), output.count());
        QCOMPARE(static_cast<QVariantList>(reply), output);
    }

    // try the template methods
    if (input.isEmpty())
        reply = iface.call(method);
    else if (input.count() == 1)
        switch (input.at(0).type())
        {
        case QVariant::Int:
            reply = iface.call(method, input.at(0).toInt());
            break;

        case QVariant::UInt:
            reply = iface.call(method, input.at(0).toUInt());
            break;

        case QVariant::String:
            reply = iface.call(method, input.at(0).toString());
            break;

        default:
            QFAIL("Unknown type. Please update the test case");
            break;
        }
    else
        reply = iface.call(method, input.at(0).toString(), input.at(1).toString());

    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    if (!output.isEmpty()) {
        QCOMPARE(reply.count(), output.count());
        QCOMPARE(static_cast<QVariantList>(reply), output);
    }
}

void tst_QDBusInterface::introspect_data()
{
    QTest::addColumn<QString>("service");
    QTest::newRow("base") << QDBusConnection().baseService();
    QTest::newRow("name") << TEST_SERVICE_NAME;
}

void tst_QDBusInterface::introspect()
{
    QFETCH(QString, service);
    QDBusConnection con;
    QDBusInterface iface(con, service, QLatin1String("/"),
                         TEST_INTERFACE_NAME);

    QDBusIntrospection::Methods mm = iface.methodData();
    QVERIFY(mm.count() == 2);

    QDBusIntrospection::Signals sm = iface.signalData();
    QVERIFY(sm.count() == 1);
    QVERIFY(sm.contains(TEST_SIGNAL_NAME));

    QDBusIntrospection::Properties pm = iface.propertyData();
    QVERIFY(pm.count() == 1);
    QVERIFY(pm.contains("prop1"));
}

void tst_QDBusInterface::signal()
{
    QDBusConnection con;
    QDBusInterface iface(con, con.baseService(), QLatin1String("/"),
                         TEST_INTERFACE_NAME);

    QString signalName = TEST_SIGNAL_NAME;

    QString arg = "So long and thanks for all the fish";
    {
        Spy spy;
        iface.connect(signalName, &spy, SLOT(spySlot(QString)));

        emitSignal(TEST_INTERFACE_NAME, signalName, arg);
        QVERIFY(spy.count == 1);
        QCOMPARE(spy.received, arg);
    }

    QDBusIntrospection::Signals sm = iface.signalData();
    QVERIFY(sm.contains(signalName));

    const QDBusIntrospection::Signal& signal = sm.value(signalName);
    QCOMPARE(signal.name, signalName);
    QVERIFY(!signal.outputArgs.isEmpty());
    {
        Spy spy;
        iface.connect(signal, &spy, SLOT(spySlot(QString)));

        emitSignal(TEST_INTERFACE_NAME, signalName, arg);
        QVERIFY(spy.count == 1);
        QCOMPARE(spy.received, arg);
    }
}

QTEST_MAIN(tst_QDBusInterface)

#include "tst_qdbusinterface.moc"


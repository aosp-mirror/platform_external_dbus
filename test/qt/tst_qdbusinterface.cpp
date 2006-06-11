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

#include "common.h"

Q_DECLARE_METATYPE(QVariantList)

#define TEST_INTERFACE_NAME "com.trolltech.QtDBus.MyObject"
#define TEST_SIGNAL_NAME "somethingHappened"

class MyObject: public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.trolltech.QtDBus.MyObject")
    Q_CLASSINFO("D-Bus Introspection", ""
"  <interface name=\"com.trolltech.QtDBus.MyObject\" >\n"
"    <property access=\"readwrite\" type=\"i\" name=\"prop1\" />\n"
"    <signal name=\"somethingHappened\" >\n"
"      <arg direction=\"out\" type=\"s\" />\n"
"    </signal>\n"
"    <method name=\"ping\" >\n"
"      <arg direction=\"in\" type=\"v\" name=\"ping\" />\n"
"      <arg direction=\"out\" type=\"v\" name=\"ping\" />\n"
"    </method>\n"
"    <method name=\"ping\" >\n"
"      <arg direction=\"in\" type=\"v\" name=\"ping1\" />\n"
"      <arg direction=\"in\" type=\"v\" name=\"ping2\" />\n"
"      <arg direction=\"out\" type=\"v\" name=\"pong1\" />\n"
"      <arg direction=\"out\" type=\"v\" name=\"pong2\" />\n"
"    </method>\n"
"  </interface>\n"
        "")
public:
    MyObject()
    {
        QObject *subObject = new QObject(this);
        subObject->setObjectName("subObject");
    }

public slots:

    void ping(const QDBusMessage &msg)
    {
        QDBusMessage reply = QDBusMessage::methodReply(msg);
        reply << static_cast<QList<QVariant> >(msg);
        if (!msg.connection().send(reply))
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
    QDBus::sessionBus().send(msg);

    QTest::qWait(200);
}

class tst_QDBusInterface: public QObject
{
    Q_OBJECT
    MyObject obj;
private slots:
    void initTestCase();

    void call_data();
    void call();

    void introspect();

    void signal();
};

void tst_QDBusInterface::initTestCase()
{
    QDBusConnection &con = QDBus::sessionBus();
    QVERIFY(con.isConnected());

    con.registerObject("/", &obj, QDBusConnection::ExportAdaptors | QDBusConnection::ExportSlots |
                       QDBusConnection::ExportChildObjects);
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
    QTest::newRow("int-int16") << "ping.n" << input << (QVariantList() << qVariantFromValue(short(1)));

    // try doing some conversions
    QVariantList output;
    output << qVariantFromValue(1U);
    QTest::newRow("int-uint") << "ping.u" << input << output;

#if QT_VERSION >= 0x040200
    output.clear();
    output << qVariantFromValue(ushort(1));
    QTest::newRow("int-uint16") << "ping.q" << input << output;
#endif

    QTest::newRow("int-int64") << "ping.x" << input << (QVariantList() << qVariantFromValue(Q_INT64_C(1)));
    QTest::newRow("int-uint64") << "ping.t" << input << (QVariantList() << qVariantFromValue(Q_UINT64_C(1)));
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
    QTest::newRow("string-int") << "ping.i" << input << output;

#if QT_VERSION >= 0x040200
    output.clear();
    output << qVariantFromValue(short(1));
    QTest::newRow("string-int16") << "ping.n" << input << input;
#endif

    output.clear();
    output << qVariantFromValue(1U);
    QTest::newRow("string-uint") << "ping.u" << input << output;

#if QT_VERSION >= 0x040200
    output.clear();
    output << qVariantFromValue(ushort(1));
    QTest::newRow("string-uint16") << "ping.q" << input << output;
#endif

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
    QDBusConnection &con = QDBus::sessionBus();
    QDBusInterface *iface = con.findInterface(con.baseService(), QLatin1String("/"),
                                              TEST_INTERFACE_NAME);

    QFETCH(QString, method);
    QFETCH(QVariantList, input);
    QFETCH(QVariantList, output);
    
    QDBusMessage reply;
    // try first callWithArgs:
    reply = iface->callWithArgs(method, input, QDBusInterface::UseEventLoop);

    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    if (!output.isEmpty()) {
        QCOMPARE(reply.count(), output.count());
        QVERIFY(compare(reply, output));
    }

    // try the template methods
    if (input.isEmpty())
        reply = iface->call(QDBusInterface::UseEventLoop, method);
    else if (input.count() == 1)
        switch (input.at(0).type())
        {
        case QVariant::Int:
            reply = iface->call(QDBusInterface::UseEventLoop, method, input.at(0).toInt());
            break;

        case QVariant::UInt:
            reply = iface->call(QDBusInterface::UseEventLoop, method, input.at(0).toUInt());
            break;

        case QVariant::String:
            reply = iface->call(QDBusInterface::UseEventLoop, method, input.at(0).toString());
            break;

        default:
            QFAIL("Unknown type. Please update the test case");
            break;
        }
    else
        reply = iface->call(QDBusInterface::UseEventLoop, method, input.at(0).toString(), input.at(1).toString());

    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    if (!output.isEmpty()) {
        QCOMPARE(reply.count(), output.count());
        QVERIFY(compare(reply, output));
    }
}

void tst_QDBusInterface::introspect()
{
    QDBusConnection &con = QDBus::sessionBus();
    QDBusInterface *iface = con.findInterface(QDBus::sessionBus().baseService(), QLatin1String("/"),
                                              TEST_INTERFACE_NAME);

    const QMetaObject *mo = iface->metaObject();

    qDebug("Improve to a better testcase of QDBusMetaObject");
    QCOMPARE(mo->methodCount() - mo->methodOffset(), 3);
    QVERIFY(mo->indexOfSignal(TEST_SIGNAL_NAME "(QString)") != -1);

    QCOMPARE(mo->propertyCount() - mo->propertyOffset(), 1);
    QVERIFY(mo->indexOfProperty("prop1") != -1);

    iface->deleteLater();
}

void tst_QDBusInterface::signal()
{
    QDBusConnection &con = QDBus::sessionBus();
    QDBusInterface *iface = con.findInterface(con.baseService(), QLatin1String("/"),
                                              TEST_INTERFACE_NAME);

    QString arg = "So long and thanks for all the fish";
    {
        Spy spy;
        spy.connect(iface, SIGNAL(somethingHappened(QString)), SLOT(spySlot(QString)));

        emitSignal(TEST_INTERFACE_NAME, TEST_SIGNAL_NAME, arg);
        QCOMPARE(spy.count, 1);
        QCOMPARE(spy.received, arg);
    }

    iface->deleteLater();
}

QTEST_MAIN(tst_QDBusInterface)

#include "tst_qdbusinterface.moc"


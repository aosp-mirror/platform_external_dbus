#include <qcoreapplication.h>
#include <qdebug.h>

#include <QtTest/QtTest>

#include <dbus/qdbus.h>

class tst_QDBusConnection: public QObject
{
    Q_OBJECT

private slots:
    void addConnection();
    void connect();
    void send();
    void sendAsync();
    void sendSignal();
};

class QDBusSpy: public QObject
{
    Q_OBJECT
public slots:
    void handlePing(const QString &str) { args.clear(); args << str; }
    void asyncReply(const QDBusMessage &msg) { args << msg; serial = msg.replySerialNumber(); }

public:
    QList<QVariant> args;
    int serial;
};

void tst_QDBusConnection::sendSignal()
{
    QDBusConnection con = QDBusConnection::addConnection(
            QDBusConnection::SessionBus);

    QVERIFY(con.isConnected());

    QDBusMessage msg = QDBusMessage::signal("/org/kde/selftest", "org.kde.selftest",
            "Ping");
    msg << QLatin1String("ping");

    QVERIFY(con.send(msg));

    QTest::qWait(1000);
}

void tst_QDBusConnection::send()
{
    QDBusConnection con = QDBusConnection::addConnection(
            QDBusConnection::SessionBus);

    QVERIFY(con.isConnected());

    QDBusMessage msg = QDBusMessage::methodCall("org.freedesktop.DBus",
            "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames");

    QDBusMessage reply = con.sendWithReply(msg);

    QCOMPARE(reply.count(), 1);
    QCOMPARE(reply.at(0).typeName(), "QStringList");
    QVERIFY(reply.at(0).toStringList().contains(con.baseService()));
}

void tst_QDBusConnection::sendAsync()
{
    QDBusConnection con = QDBusConnection::addConnection(QDBusConnection::SessionBus);
    QVERIFY(con.isConnected());

    QDBusSpy spy;

    QDBusMessage msg = QDBusMessage::methodCall("org.freedesktop.DBus",
            "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames");
    int msgId = con.sendWithReplyAsync(msg, &spy, SLOT(asyncReply(QDBusMessage)));
    QVERIFY(msgId != 0);

    QTest::qWait(1000);

    QCOMPARE(spy.args.value(0).typeName(), "QStringList");
    QVERIFY(spy.args.at(0).toStringList().contains(con.baseService()));
    QCOMPARE(spy.serial, msgId);
}

void tst_QDBusConnection::connect()
{
    QDBusSpy spy;

    QDBusConnection con = QDBusConnection::addConnection(
            QDBusConnection::SessionBus);

    con.connect("/org/kde/selftest", "org.kde.selftest", "ping", &spy,
                 SLOT(handlePing(QString)));

    QDBusMessage msg = QDBusMessage::signal("/org/kde/selftest", "org.kde.selftest",
            "ping");
    msg << QLatin1String("ping");

    QVERIFY(con.send(msg));

    QTest::qWait(1000);

    QCOMPARE(spy.args.count(), 1);
    QCOMPARE(spy.args.at(0).toString(), QString("ping"));
}

void tst_QDBusConnection::addConnection()
{
    {
        QDBusConnection con = QDBusConnection::addConnection(
                QDBusConnection::SessionBus, "bubu");

        QVERIFY(con.isConnected());
        QVERIFY(!con.lastError().isValid());

        QDBusConnection con2;
        QVERIFY(!con2.isConnected());
        QVERIFY(!con2.lastError().isValid());

        con2 = con;
        QVERIFY(con.isConnected());
        QVERIFY(con2.isConnected());
        QVERIFY(!con.lastError().isValid());
        QVERIFY(!con2.lastError().isValid());
    }

    {
        QDBusConnection con("bubu");
        QVERIFY(con.isConnected());
        QVERIFY(!con.lastError().isValid());
    }

    QDBusConnection::closeConnection("bubu");

    {
        QDBusConnection con("bubu");
        QVERIFY(!con.isConnected());
        QVERIFY(!con.lastError().isValid());
    }

    {
        {
            QDBusConnection con = QDBusConnection::addConnection(
                    QDBusConnection::SessionBus);
            QVERIFY(con.isConnected());
        }

        {
            QDBusConnection con;
            QVERIFY(con.isConnected());
            QDBusConnection::closeConnection();
            QVERIFY(con.isConnected());
        }

        {
            QDBusConnection con;
            QVERIFY(!con.isConnected());
        }
    }
}

QTEST_MAIN(tst_QDBusConnection)

#include "tst_qdbusconnection.moc"


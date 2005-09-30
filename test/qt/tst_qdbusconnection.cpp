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

    VERIFY(con.isConnected());

    QDBusMessage msg = QDBusMessage::signal("/org/kde/selftest", "org.kde.selftest",
            "Ping");
    msg << QLatin1String("ping");

    VERIFY(con.send(msg));

    QTest::wait(1000);
}

void tst_QDBusConnection::send()
{
    QDBusConnection con = QDBusConnection::addConnection(
            QDBusConnection::SessionBus);

    VERIFY(con.isConnected());

    QDBusMessage msg = QDBusMessage::methodCall("org.freedesktop.DBus",
            "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames");

    QDBusMessage reply = con.sendWithReply(msg);

    COMPARE(reply.count(), 1);
    COMPARE(reply.at(0).typeName(), "QStringList");
    VERIFY(reply.at(0).toStringList().contains(con.baseService()));
}

void tst_QDBusConnection::sendAsync()
{
    QDBusConnection con = QDBusConnection::addConnection(QDBusConnection::SessionBus);
    VERIFY(con.isConnected());

    QDBusSpy spy;

    QDBusMessage msg = QDBusMessage::methodCall("org.freedesktop.DBus",
            "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames");
    int msgId = con.sendWithReplyAsync(msg, &spy, SLOT(asyncReply(QDBusMessage)));
    VERIFY(msgId != 0);

    QTest::wait(1000);

    COMPARE(spy.args.value(0).typeName(), "QStringList");
    VERIFY(spy.args.at(0).toStringList().contains(con.baseService()));
    COMPARE(spy.serial, msgId);
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

    VERIFY(con.send(msg));

    QTest::wait(1000);

    COMPARE(spy.args.count(), 1);
    COMPARE(spy.args.at(0).toString(), QString("ping"));
}

void tst_QDBusConnection::addConnection()
{
    {
        QDBusConnection con = QDBusConnection::addConnection(
                QDBusConnection::SessionBus, "bubu");

        VERIFY(con.isConnected());
        VERIFY(!con.lastError().isValid());

        QDBusConnection con2;
        VERIFY(!con2.isConnected());
        VERIFY(!con2.lastError().isValid());

        con2 = con;
        VERIFY(con.isConnected());
        VERIFY(con2.isConnected());
        VERIFY(!con.lastError().isValid());
        VERIFY(!con2.lastError().isValid());
    }

    {
        QDBusConnection con("bubu");
        VERIFY(con.isConnected());
        VERIFY(!con.lastError().isValid());
    }

    QDBusConnection::closeConnection("bubu");

    {
        QDBusConnection con("bubu");
        VERIFY(!con.isConnected());
        VERIFY(!con.lastError().isValid());
    }

    {
        {
            QDBusConnection con = QDBusConnection::addConnection(
                    QDBusConnection::SessionBus);
            VERIFY(con.isConnected());
        }

        {
            QDBusConnection con;
            VERIFY(con.isConnected());
            QDBusConnection::closeConnection();
            VERIFY(con.isConnected());
        }

        {
            QDBusConnection con;
            VERIFY(!con.isConnected());
        }
    }
}

QTEST_MAIN(tst_QDBusConnection)

#include "tst_qdbusconnection.moc"


#include <qcoreapplication.h>
#include <qdebug.h>

#include <QtTest/QtTest>

#include <dbus/qdbus.h>

class tst_QDBusConnection: public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanupTestCase();
    void addConnection();
    void connect();
    void send();
    void sendAsync();
    void sendSignal();
    void requestName_data();
    void requestName();
    void getNameOwner_data();
    void getNameOwner();
    void releaseName_data();
    void releaseName();
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

void tst_QDBusConnection::init()
{
    if (qstrcmp(QTest::currentTestFunction(), "addConnection") == 0)
        return;

    QDBusConnection::addConnection(QDBusConnection::SessionBus);
    QVERIFY(QDBusConnection().isConnected());
}

void tst_QDBusConnection::cleanupTestCase()
{
    QDBusConnection::closeConnection();

    QVERIFY(!QDBusConnection().isConnected());
}

void tst_QDBusConnection::sendSignal()
{
    QDBusConnection con;

    QVERIFY(con.isConnected());

    QDBusMessage msg = QDBusMessage::signal("/org/kde/selftest", "org.kde.selftest",
            "Ping");
    msg << QLatin1String("ping");

    QVERIFY(con.send(msg));

    QTest::qWait(1000);
}

void tst_QDBusConnection::send()
{
    QDBusConnection con;

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
    QDBusConnection con;
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

    QDBusConnection con;

    con.connect(con.baseService(), "/org/kde/selftest", "org.kde.selftest", "ping", &spy,
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

void tst_QDBusConnection::requestName_data()
{
    QTest::addColumn<QString>("requestedName");
    QTest::addColumn<int>("flags");
    QTest::addColumn<bool>("expectedResult");

    QTest::newRow("null") << QString() << (int)QDBusConnection::NoReplace << false;
    QTest::newRow("empty") << QString("") << (int)QDBusConnection::NoReplace << false;
    QTest::newRow("invalid") << "./invalid name" << (int)QDBusConnection::NoReplace << false;
//    QTest::newRow("existing") << "org.freedesktop.DBus"
//                              << (int)QDBusConnection::NoReplace << false;

    QTest::newRow("ok1") << "com.trolltech.QtDBUS.tst_qdbusconnection"
                         << (int)QDBusConnection::NoReplace << true;
}

void tst_QDBusConnection::requestName()
{
    QDBusConnection con;

    QVERIFY(con.isConnected());
    
    QFETCH(QString, requestedName);
    QFETCH(int, flags);
    QFETCH(bool, expectedResult);

    bool result = con.requestName(requestedName, (QDBusConnection::NameRequestMode)flags);

//    QEXPECT_FAIL("existing", "For whatever reason, the bus lets us replace this name", Abort);
    QCOMPARE(result, expectedResult);
}

void tst_QDBusConnection::getNameOwner_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expectedResult");

    QTest::newRow("null") << QString() << QString();
    QTest::newRow("empty") << QString("") << QString();

    QTest::newRow("invalid") << ".invalid" << QString();
    QTest::newRow("non-existent") << "com.trolltech.QtDBUS.foo" << QString();

    QTest::newRow("bus") << "org.freedesktop.DBus" << "org.freedesktop.DBus";

    QString base = QDBusConnection().baseService();
    QTest::newRow("address") << base << base;
    QTest::newRow("self") << "com.trolltech.QtDBUS.tst_qdbusconnection" << base;
}

void tst_QDBusConnection::getNameOwner()
{
    QFETCH(QString, name);
    QFETCH(QString, expectedResult);

    QDBusConnection con;
    QVERIFY(con.isConnected());

    QString result = con.getNameOwner(name);

    QCOMPARE(result, expectedResult);
}

void tst_QDBusConnection::releaseName_data()
{
    requestName_data();
}

void tst_QDBusConnection::releaseName()
{
    QDBusConnection con;

    QVERIFY(con.isConnected());
    
    QFETCH(QString, requestedName);
    //QFETCH(int, flags);
    QFETCH(bool, expectedResult);

    bool result = con.releaseName(requestedName);

    QCOMPARE(result, expectedResult);
}    

QTEST_MAIN(tst_QDBusConnection)

#include "tst_qdbusconnection.moc"


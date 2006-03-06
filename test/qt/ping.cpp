#define DBUS_API_SUBJECT_TO_CHANGE
#include <QtCore/QtCore>
#include <QtTest/QtTest>
#include <dbus/qdbus.h>

class Ping: public QObject
{
    Q_OBJECT

public slots:
    void initTestCase();
    void cleanupTestCase();

private slots:
    void sendPing_data();
    void sendPing();

private:
    QProcess proc;
};

void Ping::initTestCase()
{
    proc.start("./qpong");
    QVERIFY(proc.waitForStarted());
    QTest::qWait(2000);
}

void Ping::cleanupTestCase()
{
    proc.close();
}

Q_DECLARE_METATYPE(QVariant)

void Ping::sendPing_data()
{
    QTest::addColumn<QVariant>("value");

    QTest::newRow("string") << QVariant("ping");
    QTest::newRow("int") << QVariant(1);
    QTest::newRow("double") << QVariant(42.5);

    QStringList strings;
    strings << "hello" << "world";
    QTest::newRow("stringlist") << QVariant(strings);

    QList<QVariant> ints;
    ints << 42 << -43 << 44 << 45;
    QTest::newRow("intlist") << QVariant(ints);

    QList<QVariant> uints;
    uints << uint(12) << uint(13) << uint(14);
    QTest::newRow("uintlist") << QVariant(uints);

    QList<QVariant> llints;
    llints << Q_INT64_C(99) << Q_INT64_C(-100);
    QTest::newRow("llintlist") << QVariant(llints);

    QList<QVariant> ullints;
    ullints << Q_UINT64_C(66) << Q_UINT64_C(67);
    QTest::newRow("ullintlist") << QVariant(ullints);

    QList<QVariant> doubles;
    doubles << 1.2 << 2.2 << 4.4;
    QTest::newRow("doublelist") << QVariant(doubles);

    QList<QVariant> stackedInts;
    stackedInts << 4 << ints << 5;
    QTest::newRow("stackedInts") << QVariant(stackedInts);

    QList<QVariant> stackedUInts;
    stackedUInts << uint(3) << uints << uint(4);
    QTest::newRow("stackedUInts") << QVariant(stackedUInts);

    QList<QVariant> stackedLlints;
    stackedLlints << Q_INT64_C(49) << llints << Q_INT64_C(-160);
    QTest::newRow("stackedLlintlist") << QVariant(stackedLlints);

    QList<QVariant> stackedUllints;
    stackedUllints << Q_UINT64_C(56) << ullints << Q_UINT64_C(57);
    QTest::newRow("stackedullintlist") << QVariant(stackedUllints);

    QList<QVariant> stackedDoubles;
    stackedDoubles << 6.2 << doubles << 6.4;
    QTest::newRow("stackedDoublelist") << QVariant(stackedDoubles);

    QMap<QString, QVariant> map;
    map["foo"] = "bar";
    map["kde"] = "great";
    QTest::newRow("map") << QVariant(map);
    
    QList<QVariant> byteArrays;
    byteArrays << QByteArray("test1") << QByteArray("t2");
    QTest::newRow("bytearray") << QVariant(byteArrays);
    
    QList<QVariant> lists;
    lists << QVariant(byteArrays) << QVariant(byteArrays);
    QTest::newRow("listoflists") << QVariant(lists);
}

void Ping::sendPing()
{
    QFETCH(QVariant, value);

    QDBusConnection &con = QDBus::sessionBus();

    QVERIFY(con.isConnected());

    QDBusMessage msg = QDBusMessage::methodCall("org.kde.selftest",
            "/org/kde/selftest", "org.kde.selftest", "ping");
    msg << value;

    QDBusMessage reply = con.sendWithReply(msg);
 //   qDebug() << reply;

    QCOMPARE(reply.count(), msg.count());
    for (int i = 0; i < reply.count(); ++i)
        QCOMPARE(reply.at(i), msg.at(i));
}

QTEST_MAIN(Ping)
#include "ping.moc"

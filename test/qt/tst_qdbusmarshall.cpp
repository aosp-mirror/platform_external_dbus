#include <QtCore/QtCore>
#include <QtTest/QtTest>
#include <dbus/qdbus.h>

#include "common.h"
#include <limits>

class tst_QDBusMarshall: public QObject
{
    Q_OBJECT

public slots:
    void initTestCase();
    void cleanupTestCase();

private slots:
    void sendBasic_data();
    void sendBasic();

    void sendVariant_data();
    void sendVariant();

    void sendArrays_data();
    void sendArrays();

    void sendArrayOfArrays_data();
    void sendArrayOfArrays();

    void sendStringMap_data();
    void sendStringMap();

    void sendStringMapOfMap_data();
    void sendStringMapOfMap();

private:
    QProcess proc;
};

void tst_QDBusMarshall::initTestCase()
{
    proc.start("./qpong");
    QVERIFY(proc.waitForStarted());
    QTest::qWait(2000);
}

void tst_QDBusMarshall::cleanupTestCase()
{
    proc.close();
    proc.kill();
}

void tst_QDBusMarshall::sendBasic_data()
{
    QTest::addColumn<QVariant>("value");
    QTest::addColumn<QString>("sig");

    // basic types:
    QTest::newRow("bool") << QVariant(false) << "b";
    QTest::newRow("bool2") << QVariant(true) << "b";
    QTest::newRow("byte") << qVariantFromValue(uchar(1)) << "y";
    QTest::newRow("int16") << qVariantFromValue(short(2)) << "n";
    QTest::newRow("uint16") << qVariantFromValue(ushort(3)) << "q";
    QTest::newRow("int") << QVariant(1) << "i";
    QTest::newRow("uint") << QVariant(2U) << "u";
    QTest::newRow("int64") << QVariant(Q_INT64_C(3)) << "x";
    QTest::newRow("uint64") << QVariant(Q_UINT64_C(4)) << "t";
    QTest::newRow("double") << QVariant(42.5) << "d";
    QTest::newRow("string") << QVariant("ping") << "s";
    QTest::newRow("emptystring") << QVariant("") << "s";
    QTest::newRow("nullstring") << QVariant(QString()) << "s";
}

void tst_QDBusMarshall::sendVariant_data()
{
    sendBasic_data();

    // add a few more:
    QVariant nested(1);
    QTest::newRow("variant") << nested << "v";

    QVariant nested2;
    qVariantSetValue(nested2, nested);
    QTest::newRow("variant-variant") << nested2 << "v";
}

void tst_QDBusMarshall::sendArrays_data()
{
    QTest::addColumn<QVariant>("value");
    QTest::addColumn<QString>("sig");

    // arrays:
    QStringList strings;
    QTest::newRow("emptystringlist") << QVariant(strings) << "as";
    strings << "hello" << "world";
    QTest::newRow("stringlist") << QVariant(strings) << "as";

    strings.clear();
    strings << "" << "" << "";
    QTest::newRow("list-of-emptystrings") << QVariant(strings) << "as";

    strings.clear();
    strings << QString() << QString() << QString() << QString();
    QTest::newRow("list-of-nullstrings") << QVariant(strings) << "as";

    QByteArray bytearray;
    QTest::newRow("nullbytearray") << QVariant(bytearray) << "ay";
    bytearray = "";             // empty, not null
    QTest::newRow("emptybytearray") << QVariant(bytearray) << "ay";
    bytearray = "foo";
    QTest::newRow("bytearray") << QVariant(bytearray) << "ay";
    bytearray.clear();
    for (int i = 0; i < 4096; ++i)
        bytearray += QByteArray(1024, char(i));
    QTest::newRow("hugebytearray") << QVariant(bytearray) << "ay";

    QList<bool> bools; 
    QTest::newRow("emptyboollist") << qVariantFromValue(bools) << "ab";
    bools << false << true << false;
    QTest::newRow("boollist") << qVariantFromValue(bools) << "ab";

    QList<short> shorts;
    QTest::newRow("emptyshortlist") << qVariantFromValue(shorts) << "an";
    shorts << 42 << -43 << 44 << 45 << -32768 << 32767;
    QTest::newRow("shortlist") << qVariantFromValue(shorts) << "an";

    QList<ushort> ushorts;
    QTest::newRow("emptyushortlist") << qVariantFromValue(ushorts) << "aq";
    ushorts << 12u << 13u << 14u << 15 << 65535;
    QTest::newRow("ushortlist") << qVariantFromValue(ushorts) << "aq";

    QList<int> ints;
    QTest::newRow("emptyintlist") << qVariantFromValue(ints) << "ai";
    ints << 42 << -43 << 44 << 45 << 2147483647 << -2147483647-1;
    QTest::newRow("intlist") << qVariantFromValue(ints) << "ai";

    QList<uint> uints;
    QTest::newRow("emptyuintlist") << qVariantFromValue(uints) << "au";
    uints << uint(12) << uint(13) << uint(14) << 4294967295U;
    QTest::newRow("uintlist") << qVariantFromValue(uints) << "au";

    QList<qlonglong> llints;
    QTest::newRow("emptyllintlist") << qVariantFromValue(llints) << "ax";
    llints << Q_INT64_C(99) << Q_INT64_C(-100)
           << Q_INT64_C(-9223372036854775807)-1 << Q_INT64_C(9223372036854775807);
    QTest::newRow("llintlist") << qVariantFromValue(llints) << "ax";

    QList<qulonglong> ullints;
    QTest::newRow("emptyullintlist") << qVariantFromValue(ullints) << "at";
    ullints << Q_UINT64_C(66) << Q_UINT64_C(67)
            << Q_UINT64_C(18446744073709551615);
    QTest::newRow("ullintlist") << qVariantFromValue(ullints) << "at";

    QList<double> doubles;
    QTest::newRow("emptydoublelist") << qVariantFromValue(doubles) << "ad";
    doubles << 1.2 << 2.2 << 4.4
            << -std::numeric_limits<double>::infinity()
            << std::numeric_limits<double>::infinity()
            << std::numeric_limits<double>::quiet_NaN();
    QTest::newRow("doublelist") << qVariantFromValue(doubles) << "ad";

    QVariantList variants;
    QTest::newRow("emptyvariantlist") << QVariant(variants) << "av";
    variants << QString("Hello") << QByteArray("World") << 42 << -43.0 << 44U << Q_INT64_C(-45)
             << Q_UINT64_C(46) << true << qVariantFromValue(short(-47));
    for (int i = 0; i < variants.count(); ++i) {
        QVariant tmp = variants.at(i);
        qVariantSetValue(variants[i], tmp);
    }
    QTest::newRow("variantlist") << QVariant(variants) << "av";
}

void tst_QDBusMarshall::sendArrayOfArrays_data()
{
    sendArrays_data();
}

void tst_QDBusMarshall::sendStringMap_data()
{
    sendBasic_data();

    QVariant nested;
    qVariantSetValue(nested, QVariant(1));
    QTest::newRow("variant") << nested << "v";

    QVariant nested2;
    qVariantSetValue(nested2, nested);
    QTest::newRow("variant-variant") << nested2 << "v";

    sendArrays_data();
}

void tst_QDBusMarshall::sendStringMapOfMap_data()
{
    sendStringMap_data();
}

void tst_QDBusMarshall::sendBasic()
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
    QTEST(reply.signature(), "sig");
    for (int i = 0; i < reply.count(); ++i)
        QVERIFY(compare(reply.at(i), msg.at(i)));
}

void tst_QDBusMarshall::sendVariant()
{
    QFETCH(QVariant, value);
    QVariant tmp = value;
    qVariantSetValue(value, tmp);

    QDBusConnection &con = QDBus::sessionBus();

    QVERIFY(con.isConnected());

    QDBusMessage msg = QDBusMessage::methodCall("org.kde.selftest",
            "/org/kde/selftest", "org.kde.selftest", "ping");
    msg << value;

    QDBusMessage reply = con.sendWithReply(msg);
 //   qDebug() << reply;

    QCOMPARE(reply.count(), msg.count());
    QCOMPARE(reply.signature(), QString("v"));
    for (int i = 0; i < reply.count(); ++i)
        QVERIFY(compare(reply.at(i), msg.at(i)));
}

void tst_QDBusMarshall::sendArrays()
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
    QTEST(reply.signature(), "sig");
    for (int i = 0; i < reply.count(); ++i)
        QVERIFY(compare(reply.at(i), msg.at(i)));
}

void tst_QDBusMarshall::sendArrayOfArrays()
{
    QFETCH(QVariant, value);

    QDBusConnection &con = QDBus::sessionBus();

    QVERIFY(con.isConnected());

    QDBusMessage msg = QDBusMessage::methodCall("org.kde.selftest",
            "/org/kde/selftest", "org.kde.selftest", "ping");
    msg << QVariant(QVariantList() << value << value);

    QDBusMessage reply = con.sendWithReply(msg);
 //   qDebug() << reply;

    QCOMPARE(reply.count(), msg.count());
    QFETCH(QString, sig);
    QCOMPARE(reply.signature(), "a" + sig);
    for (int i = 0; i < reply.count(); ++i)
        QVERIFY(compare(reply.at(i), msg.at(i)));
}

void tst_QDBusMarshall::sendStringMap()
{
    QFETCH(QVariant, value);

    QDBusConnection &con = QDBus::sessionBus();

    QVERIFY(con.isConnected());

    QDBusMessage msg = QDBusMessage::methodCall("org.kde.selftest",
            "/org/kde/selftest", "org.kde.selftest", "ping");

    QVariantMap map;
    map["foo"] = value;
    map["bar"] = value;
    msg << QVariant(map);

    QDBusMessage reply = con.sendWithReply(msg);
 //   qDebug() << reply;

    QCOMPARE(reply.count(), msg.count());
    QFETCH(QString, sig);
    QCOMPARE(reply.signature(), "a{s" + sig + "}");
    for (int i = 0; i < reply.count(); ++i)
        QVERIFY(compare(reply.at(i), msg.at(i)));
}

void tst_QDBusMarshall::sendStringMapOfMap()
{
    QFETCH(QVariant, value);

    QDBusConnection &con = QDBus::sessionBus();

    QVERIFY(con.isConnected());

    QDBusMessage msg = QDBusMessage::methodCall("org.kde.selftest",
            "/org/kde/selftest", "org.kde.selftest", "ping");

    QVariantMap map;
    map["foo"] = value;
    map["bar"] = value;

    QVariantMap map2;
    map2["foo"] = map;
    msg << QVariant(map2);

    QDBusMessage reply = con.sendWithReply(msg);
 //   qDebug() << reply;

    QCOMPARE(reply.count(), msg.count());
    QFETCH(QString, sig);
    QCOMPARE(reply.signature(), "a{sa{s" + sig + "}}");

    for (int i = 0; i < reply.count(); ++i)
        QVERIFY(compare(reply.at(i), msg.at(i)));
}


QTEST_MAIN(tst_QDBusMarshall)
#include "tst_qdbusmarshall.moc"

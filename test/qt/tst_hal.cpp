#include <qcoreapplication.h>
#include <qdebug.h>

#include <QtTest/QtTest>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/qdbus.h>

class tst_Hal: public QObject
{
    Q_OBJECT

private slots:
    void getDevices();
    void lock();
};

class Spy: public QObject
{
    Q_OBJECT
public:
    int count;
    QDBusConnection &conn;

    Spy(QDBusConnection &c) : count(0), conn(c)
    { }

public slots:
    void spySlot(int, const QVariantList&)
    {
        ++count;
        QDBusMessage msg = QDBusMessage::methodCall("org.freedesktop.Hal",
                        "/org/freedesktop/Hal/devices/acpi_CPU0",
                        "org.freedesktop.Hal.Device", "GetProperty");
        msg << "info.locked";

        QDBusMessage reply = conn.sendWithReply(msg);
        QVERIFY(!reply.isEmpty());
    }
};


void tst_Hal::getDevices()
{
    QDBusConnection &con = QDBus::systemBus();
    QVERIFY(con.isConnected());

    QDBusMessage msg = QDBusMessage::methodCall("org.freedesktop.Hal",
                "/org/freedesktop/Hal/Manager", "org.freedesktop.Hal.Manager",
                "GetAllDevices");

    QDBusMessage reply = con.sendWithReply(msg);
    QVERIFY(!reply.isEmpty());
    QVERIFY(reply.type() == QDBusMessage::ReplyMessage);
    //qDebug() << reply;
}

void tst_Hal::lock()
{
    QDBusConnection &con = QDBus::systemBus();
    QVERIFY(con.isConnected());

    Spy spy( con );

    con.connect("org.freedesktop.Hal", "/org/freedesktop/Hal/devices/acpi_CPU0",
                "org.freedesktop.Hal.Device", "PropertyModified",
                &spy, SLOT(spySlot(int, QVariantList)));
    QDBusMessage msg = QDBusMessage::methodCall("org.freedesktop.Hal",
                "/org/freedesktop/Hal/devices/acpi_CPU0", "org.freedesktop.Hal.Device",
                "Lock");
    msg << "No reason...";

    QDBusMessage reply = con.sendWithReply(msg);
    //QTest::qWait(200);
    //qDebug() << reply;
    QCOMPARE(spy.count, 3);
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
}

QTEST_MAIN(tst_Hal)

#include "tst_hal.moc"

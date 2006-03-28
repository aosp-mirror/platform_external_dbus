#include <QtCore/QtCore>
#include <dbus/qdbus.h>
#include <dbus/dbus.h>

class Pong: public QObject
{
    Q_OBJECT
public slots:

    void ping(const QDBusMessage &msg)
    {
        QDBusMessage reply = QDBusMessage::methodReply(msg);
        reply << static_cast<QList<QVariant> >(msg);
        reply.setSignature(msg.signature());
        if (!msg.connection().send(reply))
            exit(1);
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QDBusConnection &con = QDBus::sessionBus();
    QDBusMessage msg = QDBusMessage::methodCall(DBUS_SERVICE_DBUS,
                                                DBUS_PATH_DBUS,
                                                DBUS_INTERFACE_DBUS,
                                                "RequestName");
    msg << "org.kde.selftest" << 0U;
    msg = con.sendWithReply(msg);
    if (msg.type() != QDBusMessage::ReplyMessage)
        exit(2);

    Pong pong;
    con.registerObject("/org/kde/selftest", &pong, QDBusConnection::ExportSlots);

    printf("ready.\n");

    return app.exec();
}

#include "qpong.moc"

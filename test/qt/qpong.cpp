#define DBUS_API_SUBJECT_TO_CHANGE
#include <QtCore/QtCore>
#include <dbus/qdbus.h>

class Pong: public QObject
{
    Q_OBJECT
public slots:

    void ping(const QDBusMessage &msg)
    {
        QDBusMessage reply = QDBusMessage::methodReply(msg);
        reply << static_cast<QList<QVariant> >(msg);
        if (!msg.connection().send(reply))
            exit(1);
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QDBusConnection &con = QDBus::sessionBus();
    if (!con.requestName("org.kde.selftest"))
        exit(2);

    Pong pong;
    con.registerObject("/org/kde/selftest", &pong, QDBusConnection::ExportSlots);

    printf("ready.\n");

    return app.exec();
}

#include "qpong.moc"

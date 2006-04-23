/* -*- C++ -*-
 *
 * Copyright (C) 2006 Trolltech AS. All rights reserved.
 *    Author: Thiago Macieira <thiago.macieira@trolltech.com>
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

#include <stdio.h>

#include <dbus/qdbus.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QStringList>
#include <QtCore/qmetaobject.h>
#include <QtXml/QDomDocument>
#include <QtXml/QDomElement>

Q_DECLARE_METATYPE(QVariant)
QDBusConnection *connection;

void listObjects(const QString &service, const QString &path)
{
    QDBusInterface *iface = connection->findInterface(service, path.isEmpty() ? "/" : path,
                                                     "org.freedesktop.DBus.Introspectable");
    QDBusReply<QString> xml = iface->call("Introspect");

    if (xml.isError())
        return;                 // silently

    QDomDocument doc;
    doc.setContent(xml);
    QDomElement node = doc.documentElement();
    QDomElement child = node.firstChildElement();
    while (!child.isNull()) {
        if (child.tagName() == QLatin1String("node")) {
            QString sub = path + '/' + child.attribute("name");
            printf("%s\n", qPrintable(sub));
            listObjects(service, sub);
        }
        child = child.nextSiblingElement();
    }

    delete iface;
}

void listInterface(const QString &service, const QString &path, const QString &interface)
{
    QDBusInterface *iface = connection->findInterface(service, path, interface);
    const QMetaObject *mo = iface->metaObject();

    // properties
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        QMetaProperty mp = mo->property(i);
        printf("property ");

        if (mp.isReadable() && mp.isWritable())
            printf("readwrite");
        else if (mp.isReadable())
            printf("read");
        else
            printf("write");

        printf(" %s %s.%s\n", mp.typeName(), qPrintable(interface), mp.name());
    }

    // methods (signals and slots)
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        QMetaMethod mm = mo->method(i);

        QByteArray signature = mm.signature();
        signature.truncate(signature.indexOf('('));
        printf("%s %s%s%s %s.%s(",
               mm.methodType() == QMetaMethod::Signal ? "signal" : "method",
               mm.tag(), *mm.tag() ? " " : "",
               *mm.typeName() ? mm.typeName() : "void",
               qPrintable(interface), signature.constData());

        QList<QByteArray> types = mm.parameterTypes();
        QList<QByteArray> names = mm.parameterNames();
        bool first = true;
        for (int i = 0; i < types.count(); ++i) {
            printf("%s%s",
                   first ? "" : ", ",
                   types.at(i).constData());
            if (!names.at(i).isEmpty())
                printf(" %s", names.at(i).constData());
            first = false;
        }
        printf(")\n");
    }
    delete iface;
}

void listAllInterfaces(const QString &service, const QString &path)
{
    QDBusInterface *iface = connection->findInterface(service, path,
                                                     "org.freedesktop.DBus.Introspectable");
    QDBusReply<QString> xml = iface->call("Introspect");

    if (xml.isError())
        return;                 // silently

    QDomDocument doc;
    doc.setContent(xml);
    QDomElement node = doc.documentElement();
    QDomElement child = node.firstChildElement();
    while (!child.isNull()) {
        if (child.tagName() == QLatin1String("interface")) {
            listInterface(service, path, child.attribute("name"));
        }
        child = child.nextSiblingElement();
    }

    delete iface;
}

QStringList readList(int &argc, const char *const *&argv)
{
    --argc;
    ++argv;

    QStringList retval;
    while (argc && QLatin1String(argv[0]) != ")")
        retval += QString::fromLocal8Bit(argv[0]);

    return retval;
}

void placeCall(const QString &service, const QString &path, const QString &interface,
               const QString &member, int argc, const char *const *argv)
{
    QDBusInterface *iface;
    iface = connection->findInterface(service, path, interface);

    if (!iface) {
        fprintf(stderr, "Interface '%s' not available in object %s at %s\n",
                qPrintable(interface), qPrintable(path), qPrintable(service));
        exit(1);
    }

    const QMetaObject *mo = iface->metaObject();
    QByteArray match = member.toLatin1();
    match += '(';

    int midx;
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        QMetaMethod mm = mo->method(i);
        QByteArray signature = mm.signature();
        if (signature.startsWith(match)) {
            midx = i;
            break;
        }
    }

    if (midx == -1) {
        fprintf(stderr, "Cannot find '%s.%s' in object %s at %s\n",
                qPrintable(interface), qPrintable(member), qPrintable(path),
                qPrintable(service));
        exit(1);
    }

    QMetaMethod mm = iface->metaObject()->method(midx);
    QList<QByteArray> types = mm.parameterTypes();

    QVariantList params;
    for (int i = 0; argc && i < types.count(); ++i) {
        int id = QVariant::nameToType(types.at(i));
        if ((id == QVariant::UserType || id == QVariant::Map) && types.at(i) != "QVariant") {
            fprintf(stderr, "Sorry, can't pass arg of type %s yet\n",
                    types.at(i).constData());
            exit(1);
        }
        if (id == QVariant::UserType)
            id = QMetaType::type(types.at(i));

        Q_ASSERT(id);

        QVariant p;
        if ((id == QVariant::List || id == QVariant::StringList) && QLatin1String("(") == argv[0])
            p = readList(argc, argv);
        else
            p = QString::fromLocal8Bit(argv[0]);

        if (id < int(QVariant::UserType))
            // avoid calling it for QVariant
            p.convert( QVariant::Type(id) );
        else if (types.at(i) == "QVariant") {
            QVariant tmp(id, p.constData());
            p = tmp;
        }
        params += p;
        --argc;
        ++argv;
    }
    if (params.count() != types.count()) {
        fprintf(stderr, "Invalid number of parameters\n");
        exit(1);
    }

    QDBusMessage reply = iface->callWithArgs(member, params);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        QDBusError err = reply;
        printf("Error: %s\n%s\n", qPrintable(err.name()), qPrintable(err.message()));
        exit(2);
    } else if (reply.type() != QDBusMessage::ReplyMessage) {
        fprintf(stderr, "Invalid reply type %d\n", int(reply.type()));
        exit(1);
    }
    
    foreach (QVariant v, reply) {
        if (v.userType() == qMetaTypeId<QVariant>())
            v = qvariant_cast<QVariant>(v);
        printf("%s\n", qPrintable(v.toString()));
    }

    delete iface;
    exit(0);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc >= 1 && qstrcmp(argv[1], "--system") == 0) {
        connection = &QDBus::systemBus();
        --argc;
        ++argv;
    } else
        connection = &QDBus::sessionBus();

    if (!connection->isConnected()) {
        fprintf(stderr, "Could not connect to D-Bus server: %s: %s\n",
                qPrintable(connection->lastError().name()),
                qPrintable(connection->lastError().message()));
        return 1;
    }
    QDBusBusService *bus = connection->busService();

    if (argc == 1) {
        QStringList names = bus->ListNames();
        foreach (QString name, names)
            printf("%s\n", qPrintable(name));
        exit(0);
    }
    
    QString service = QLatin1String(argv[1]);
    if (!QDBusUtil::isValidBusName(service)) {
        fprintf(stderr, "Service '%s' is not a valid name.\n", qPrintable(service));
        exit(1);
    }
    if (!bus->NameHasOwner(service)) {
        fprintf(stderr, "Service '%s' does not exist.\n", qPrintable(service));
        exit(1);
    }

    if (argc == 2) {
        printf("/\n");
        listObjects(service, QString());
        exit(0);
    }

    QString path = QLatin1String(argv[2]);
    if (!QDBusUtil::isValidObjectPath(path)) {
        fprintf(stderr, "Path '%s' is not a valid path name.\n", qPrintable(path));
        exit(1);
    }
    if (argc == 3) {
        listAllInterfaces(service, path);
        exit(0);
    }

    QString interface = QLatin1String(argv[3]);
    QString member;
    int pos = interface.lastIndexOf(QLatin1Char('.'));
    if (pos == -1) {
        member = interface;
        interface.clear();
    } else {
        member = interface.mid(pos + 1);
        interface.truncate(pos);
    }

    placeCall(service, path, interface, member, argc - 4, argv + 4);
}


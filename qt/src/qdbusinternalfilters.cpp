/* -*- mode: C++ -*-
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
 * along with this program; if not, write to the Free Software Foundation
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "qdbusconnection_p.h"

#include <dbus/dbus.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qstringlist.h>

#include "qdbusabstractadaptor.h"
#include "qdbusabstractadaptor_p.h"
#include "qdbusconnection.h"
#include "qdbusmessage.h"
#include "qdbustypehelper_p.h"
#include "qdbusutil.h"

// defined in qdbusxmlgenerator.cpp
extern QString qDBusGenerateMetaObjectXml(QString interface, const QMetaObject *mo,
                                          const QMetaObject *base, int flags);

static const char introspectableInterfaceXml[] =
    "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
    "    <method name=\"Introspect\">\n"
    "      <arg name=\"xml_data\" type=\"s\" direction=\"out\"/>\n"
    "    </method>\n"
    "  </interface>\n";

static const char propertiesInterfaceXml[] =
    "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
    "    <method name=\"Get\">\n"
    "      <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"
    "      <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"
    "      <arg name=\"value\" type=\"v\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <method name=\"Set\">\n"
    "      <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"
    "      <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"
    "      <arg name=\"value\" type=\"v\" direction=\"in\"/>\n"
    "    </method>\n"
    "  </interface>\n";

static QString generateSubObjectXml(QObject *object)
{
    QString retval;
    foreach (QObject *child, object->children()) {
        QString name = child->objectName();
        if (!name.isEmpty())
            retval += QString(QLatin1String("  <node name=\"%1\"/>\n"))
                      .arg(name);
    }
    return retval;
}

QString qDBusIntrospectObject(const QDBusConnectionPrivate::ObjectTreeNode *node)
{
    // object may be null

    QString xml_data(QLatin1String(DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE));
    xml_data += QLatin1String("<node>\n");

    if (node->obj) {
        if (node->flags & QDBusConnection::ExportContents) {
            const QMetaObject *mo = node->obj->metaObject();
            for ( ; mo != &QObject::staticMetaObject; mo = mo->superClass())
                xml_data += qDBusGenerateMetaObjectXml(QString(), mo, mo->superClass(),
                                                  node->flags);
        }

        // does this object have adaptors?
        QDBusAdaptorConnector *connector;
        if (node->flags & QDBusConnection::ExportAdaptors &&
            (connector = qDBusFindAdaptorConnector(node->obj))) {

            // trasverse every adaptor in this object
            QDBusAdaptorConnector::AdaptorMap::ConstIterator it = connector->adaptors.constBegin();
            QDBusAdaptorConnector::AdaptorMap::ConstIterator end = connector->adaptors.constEnd();
            for ( ; it != end; ++it) {
                // add the interface:
                QString ifaceXml = QDBusAbstractAdaptorPrivate::retrieveIntrospectionXml(it->adaptor);
                if (ifaceXml.isEmpty()) {
                    // add the interface's contents:
                    ifaceXml += qDBusGenerateMetaObjectXml(it->interface, it->metaObject,
                                                           &QDBusAbstractAdaptor::staticMetaObject,
                                                           QDBusConnection::ExportAllContents);

                    QDBusAbstractAdaptorPrivate::saveIntrospectionXml(it->adaptor, ifaceXml);
                }

                xml_data += ifaceXml;
            }
        }

        xml_data += QLatin1String( introspectableInterfaceXml );
        xml_data += QLatin1String( propertiesInterfaceXml );
    }

    if (node->flags & QDBusConnection::ExportChildObjects) {
        xml_data += generateSubObjectXml(node->obj);
    } else {
        // generate from the object tree
        foreach (const QDBusConnectionPrivate::ObjectTreeNode::Data &entry, node->children) {
            if (entry.node && (entry.node->obj || !entry.node->children.isEmpty()))
                xml_data += QString(QLatin1String("  <node name=\"%1\"/>\n"))
                            .arg(entry.name);
        }
    }

    xml_data += QLatin1String("</node>\n");
    return xml_data;
}

void qDBusIntrospectObject(const QDBusConnectionPrivate::ObjectTreeNode *node,
                           const QDBusMessage &msg)
{
    // now send it
    QDBusMessage reply = QDBusMessage::methodReply(msg);
    reply << qDBusIntrospectObject(node);
    msg.connection().send(reply);
}

// implement the D-Bus interface org.freedesktop.DBus.Properties

static void sendPropertyError(const QDBusMessage &msg, const QString &interface_name)
{
    QDBusMessage error = QDBusMessage::error(msg, QLatin1String(DBUS_ERROR_INVALID_ARGS),
                                   QString::fromLatin1("Interface %1 was not found in object %2")
                                   .arg(interface_name)
                                   .arg(msg.path()));
    msg.connection().send(error);
}

void qDBusPropertyGet(const QDBusConnectionPrivate::ObjectTreeNode *node, const QDBusMessage &msg)
{
    Q_ASSERT(msg.count() == 2);
    QString interface_name = msg.at(0).toString();
    QByteArray property_name = msg.at(1).toString().toUtf8();

    QDBusAdaptorConnector *connector;
    QVariant value;
    if (node->flags & QDBusConnection::ExportAdaptors &&
        (connector = qDBusFindAdaptorConnector(node->obj))) {

        // find the class that implements interface_name
        QDBusAdaptorConnector::AdaptorMap::ConstIterator it;
        it = qLowerBound(connector->adaptors.constBegin(), connector->adaptors.constEnd(),
                         interface_name);
        if (it != connector->adaptors.constEnd() && it->interface == interface_name)
            value = it->adaptor->property(property_name);
    }

    if (!value.isValid() && node->flags & QDBusConnection::ExportProperties) {
        // try the object itself
        int pidx = node->obj->metaObject()->indexOfProperty(property_name);
        if (pidx != -1) {
            QMetaProperty mp = node->obj->metaObject()->property(pidx);
            if (mp.isScriptable() || (node->flags & QDBusConnection::ExportAllProperties) ==
                QDBusConnection::ExportAllProperties)
                value = mp.read(node->obj);
        }
    }

    if (!value.isValid()) {
        // the property was not found
        sendPropertyError(msg, interface_name);
        return;
    }

    QDBusMessage reply = QDBusMessage::methodReply(msg);
    reply.setSignature(QLatin1String("v"));
    reply << value;
    msg.connection().send(reply);
}

void qDBusPropertySet(const QDBusConnectionPrivate::ObjectTreeNode *node, const QDBusMessage &msg)
{
    Q_ASSERT(msg.count() == 3);
    QString interface_name = msg.at(0).toString();
    QByteArray property_name = msg.at(1).toString().toUtf8();
    QVariant value = QDBusTypeHelper<QVariant>::fromVariant(msg.at(2));

    QDBusAdaptorConnector *connector;
    if (node->flags & QDBusConnection::ExportAdaptors &&
        (connector = qDBusFindAdaptorConnector(node->obj))) {

        // find the class that implements interface_name
        QDBusAdaptorConnector::AdaptorMap::ConstIterator it;
        it = qLowerBound(connector->adaptors.constBegin(), connector->adaptors.constEnd(),
                         interface_name);
        if (it != connector->adaptors.end() && it->interface == interface_name)
            if (it->adaptor->setProperty(property_name, value)) {
                msg.connection().send(QDBusMessage::methodReply(msg));
                return;
            }
    }

    if (node->flags & QDBusConnection::ExportProperties) {
        // try the object itself
        int pidx = node->obj->metaObject()->indexOfProperty(property_name);
        if (pidx != -1) {
            QMetaProperty mp = node->obj->metaObject()->property(pidx);
            if (mp.isScriptable() || (node->flags & QDBusConnection::ExportAllProperties) ==
                QDBusConnection::ExportAllProperties) {

                if (mp.write(node->obj, value)) {
                    msg.connection().send(QDBusMessage::methodReply(msg));
                    return;
                }
            }
        }
    }

    // the property was not found or not written to
    sendPropertyError(msg, interface_name);
}

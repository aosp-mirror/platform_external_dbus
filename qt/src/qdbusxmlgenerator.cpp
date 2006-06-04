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

#include <QtCore/qcoreapplication.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qstringlist.h>

#include "qdbusinterface_p.h"   // for ANNOTATION_NO_WAIT
#include "qdbusabstractadaptor_p.h" // for QCLASSINFO_DBUS_*
#include "qdbusconnection_p.h"  // for the flags
#include "qdbusutil.h"

extern QDBUS_EXPORT QString qDBusGenerateMetaObjectXml(QString interface, const QMetaObject *mo,
                                                       const QMetaObject *base, int flags);

// implement the D-Bus org.freedesktop.DBus.Introspectable interface
// we do that by analysing the metaObject of all the adaptor interfaces

static QString generateInterfaceXml(const QMetaObject *mo, int flags, int methodOffset, int propOffset)
{
    QString retval;

    // start with properties:
    if (flags & QDBusConnection::ExportProperties) {
        for (int i = propOffset; i < mo->propertyCount(); ++i) {
            static const char *accessvalues[] = {0, "read", "write", "readwrite"};

            QMetaProperty mp = mo->property(i);

            if (!mp.isScriptable() && (flags & QDBusConnection::ExportAllProperties) !=
                QDBusConnection::ExportAllProperties)
                continue;

            int access = 0;
            if (mp.isReadable())
                access |= 1;
            if (mp.isWritable())
                access |= 2;

            int typeId = qDBusNameToTypeId(mp.typeName());
            if (!typeId)
                continue;

            retval += QString(QLatin1String("    <property name=\"%1\" type=\"%2\" access=\"%3\" />\n"))
                      .arg(mp.name())
                      .arg(QLatin1String( QDBusUtil::typeToSignature( QVariant::Type(typeId) )))
                      .arg(QLatin1String( accessvalues[access] ));
        }
    }

    // now add methods:
    for (int i = methodOffset; i < mo->methodCount(); ++i) {
        QMetaMethod mm = mo->method(i);
        QByteArray signature = mm.signature();
        int paren = signature.indexOf('(');

        bool isSignal;
        if (mm.methodType() == QMetaMethod::Signal)
            // adding a signal
            isSignal = true;
        else if (mm.methodType() == QMetaMethod::Slot && mm.access() == QMetaMethod::Public)
            isSignal = false;
        else
            continue;           // neither signal nor public slot

        if ((isSignal && !(flags & QDBusConnection::ExportSignals)) ||
            (!isSignal && !(flags & QDBusConnection::ExportSlots)))
            continue;

        QString xml = QString(QLatin1String("    <%1 name=\"%2\">\n"))
                      .arg(isSignal ? QLatin1String("signal") : QLatin1String("method"))
                      .arg(QLatin1String(signature.left(paren)));

        // check the return type first
        int typeId = qDBusNameToTypeId(mm.typeName());
        if (typeId)
            xml += QString(QLatin1String("      <arg type=\"%1\" direction=\"out\"/>\n"))
                   .arg(QLatin1String(QDBusUtil::typeToSignature( QVariant::Type(typeId) )));
        else if (*mm.typeName())
            continue;           // wasn't a valid type

        QList<QByteArray> names = mm.parameterNames();
        QList<int> types;
        int inputCount = qDBusParametersForMethod(mm, types);
        if (inputCount == -1)
            continue;           // invalid form
        if (isSignal && inputCount + 1 != types.count())
            continue;           // signal with output arguments?
        if (isSignal && types.at(inputCount) == QDBusConnectionPrivate::messageMetaType)
            continue;           // signal with QDBusMessage argument?

        int j;
        bool isScriptable = mm.attributes() & QMetaMethod::Scriptable;
        for (j = 1; j < types.count(); ++j) {
            // input parameter for a slot or output for a signal
            if (types.at(j) == QDBusConnectionPrivate::messageMetaType) {
                isScriptable = true;
                continue;
            }

            QString name;
            if (!names.at(j - 1).isEmpty())
                name = QString(QLatin1String("name=\"%1\" ")).arg(QLatin1String(names.at(j - 1)));

            bool isOutput = isSignal || j > inputCount;

            xml += QString(QLatin1String("      <arg %1type=\"%2\" direction=\"%3\"/>\n"))
                   .arg(name)
                   .arg(QLatin1String(QDBusUtil::typeToSignature( QVariant::Type(types.at(j)) )))
                   .arg(isOutput ? QLatin1String("out") : QLatin1String("in"));
        }

        if (!isScriptable) {
            // check if this was added by other means
            if (isSignal && (flags & QDBusConnection::ExportAllSignals) != QDBusConnection::ExportAllSignals)
                continue;
            if (!isSignal && (flags & QDBusConnection::ExportAllSlots) != QDBusConnection::ExportAllSlots)
                continue;
        }

        if (qDBusCheckAsyncTag(mm.tag()))
            // add the no-reply annotation
            xml += QLatin1String("      <annotation name=\"" ANNOTATION_NO_WAIT "\""
                                 " value=\"true\"/>\n");

        retval += xml;
        retval += QString(QLatin1String("    </%1>\n"))
                  .arg(isSignal ? QLatin1String("signal") : QLatin1String("method"));
    }

    return retval;
}

QString qDBusGenerateMetaObjectXml(QString interface, const QMetaObject *mo, const QMetaObject *base,
                                   int flags)
{
    if (interface.isEmpty()) {
        // generate the interface name from the meta object
        int idx = mo->indexOfClassInfo(QCLASSINFO_DBUS_INTERFACE);
        if (idx >= mo->classInfoOffset()) {
            interface = QLatin1String(mo->classInfo(idx).value());
        } else {
            interface = QLatin1String(mo->className());
            interface.replace(QLatin1String("::"), QLatin1String("."));

            if (interface.startsWith( QLatin1String("QDBus") )) {
                interface.prepend( QLatin1String("com.trolltech.QtDBus.") );
            } else if (interface.startsWith( QLatin1Char('Q') )) {
                // assume it's Qt
                interface.prepend( QLatin1String("com.trolltech.Qt.") );
            } else if (!QCoreApplication::instance() ||
                       QCoreApplication::instance()->applicationName().isEmpty()) {
                interface.prepend( QLatin1String("local.") );
            } else {
                interface.prepend(QLatin1Char('.')).prepend( QCoreApplication::instance()->applicationName() );
                QStringList domainName = QCoreApplication::instance()->organizationDomain().split(QLatin1Char('.'));
                foreach (const QString &part, domainName)
                    interface.prepend(QLatin1Char('.')).prepend(part);
            }
        }
    }

    QString xml;
    int idx = mo->indexOfClassInfo(QCLASSINFO_DBUS_INTROSPECTION);
    if (idx >= mo->classInfoOffset())
        return QString::fromUtf8(mo->classInfo(idx).value());
    else
        xml = generateInterfaceXml(mo, flags, base->methodCount(), base->propertyCount());

    if (xml.isEmpty())
        return QString();       // don't add an empty interface
    return QString(QLatin1String("  <interface name=\"%1\">\n%2  </interface>\n"))
        .arg(interface, xml);
}

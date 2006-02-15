/* -*- C++ -*-
 *
 * Copyright (C) 2005 Thiago Macieira <thiago@kde.org>
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "qdbusxmlparser_p.h"
#include "qdbusinterface.h"
#include "qdbusinterface_p.h"
#include "qdbusconnection_p.h"
#include "qdbusobject_p.h"

#include <QtXml/qdom.h>
#include <QtCore/qmap.h>
#include <QtCore/qvariant.h>
#include <QtCore/qtextstream.h>

static QDBusIntrospection::Annotations
parseAnnotations(const QDomElement& elem)
{
    QDBusIntrospection::Annotations retval;
    QDomNodeList list = elem.elementsByTagName("annotation");
    for (int i = 0; i < list.count(); ++i)
    {
        QDomElement ann = list.item(i).toElement();
        if (ann.isNull())
            continue;
        
        QString name = ann.attribute("name"),
               value = ann.attribute("value");

        if (name.isEmpty())
            continue;

        retval.insert(name, value);
    }
    
    return retval;
}

static QDBusType
parseType(const QString& type)
{
    if (type.isEmpty())
        return QDBusType();
    return QDBusType(type);
}

static QDBusIntrospection::Arguments
parseArgs(const QDomElement& elem, const QLatin1String& direction, bool acceptEmpty = false)
{
    QDBusIntrospection::Arguments retval;
    QDomNodeList list = elem.elementsByTagName("arg");
    for (int i = 0; i < list.count(); ++i)
    {
        QDomElement arg = list.item(i).toElement();
        if (arg.isNull())
            continue;

        if ((acceptEmpty && !arg.hasAttribute("direction")) ||
            arg.attribute("direction") == direction) {
            
            QDBusIntrospection::Argument argData;
            if (arg.hasAttribute("name"))
                argData.name = arg.attribute("name"); // can be empty
            argData.type = parseType(arg.attribute("type"));
            
            if (!argData.type.isValid())
                continue;
            
            retval << argData;
        }
    }
    return retval;
}

QDBusXmlParser::QDBusXmlParser(const QString& service, const QString& path,
                               const QString& xmlData, QDBusConnectionPrivate* store)
    : m_service(service), m_path(path), m_store(store)
{
    QDomDocument doc;
    doc.setContent(xmlData);
    m_node = doc.firstChildElement("node");
}

QDBusXmlParser::QDBusXmlParser(const QString& service, const QString& path,
                               const QDomElement& node, QDBusConnectionPrivate* store)
    : m_service(service), m_path(path), m_node(node), m_store(store)
{
}

void QDBusXmlParser::parse(const QDBusObjectPrivate* d, const QString &xml)
{
    QDBusXmlParser parser(d->data->service, d->data->path, xml, d->parent);
    parser.object();
    parser.interfaces();
}

QDBusIntrospection::Interfaces
QDBusXmlParser::interfaces() const
{
    QDBusIntrospection::Interfaces retval;

    if (m_node.isNull())
        return retval;
        
    QDomNodeList interfaces = m_node.elementsByTagName("interface");
    for (int i = 0; i < interfaces.count(); ++i)
    {
        QDomElement iface = interfaces.item(i).toElement();
        QString ifaceName = iface.attribute("name");
        if (iface.isNull() || ifaceName.isEmpty())
            continue;           // for whatever reason

        QDBusIntrospection::Interface *ifaceData;
        if (m_store) {
            QSharedDataPointer<QDBusIntrospection::Interface> knownData =
                m_store->findInterface(ifaceName);
            if (!knownData.constData()->introspection.isEmpty()) {
                // it's already known
                // we don't have to re-parse
                retval.insert(ifaceName, knownData);
                continue;
            }

            // ugly, but ok
            // we don't want to detach
            // we *WANT* to modify the shared data
            ifaceData = const_cast<QDBusIntrospection::Interface*>( knownData.constData() );
        }
        else {
            ifaceData = new QDBusIntrospection::Interface;
            ifaceData->name = ifaceName;
        }

        {
            // save the data
            QTextStream ts(&ifaceData->introspection);
            iface.save(ts,2);
        }

        // parse annotations
        ifaceData->annotations = parseAnnotations(iface);

        // parse methods
        QDomNodeList list = iface.elementsByTagName("method");
        for (int j = 0; j < list.count(); ++j)
        {
            QDomElement method = list.item(j).toElement();
            QString methodName = method.attribute("name");
            if (method.isNull() || methodName.isEmpty())
                continue;

            QDBusIntrospection::Method methodData;
            methodData.name = methodName;

            // parse arguments
            methodData.inputArgs = parseArgs(method, QLatin1String("in"));
            methodData.outputArgs = parseArgs(method, QLatin1String("out"));
            methodData.annotations = parseAnnotations(method);

            // add it
            ifaceData->methods.insert(methodName, methodData);                
        }

        // parse signals
        list = iface.elementsByTagName("signal");
        for (int j = 0; j < list.count(); ++j)
        {
            QDomElement signal = list.item(j).toElement();
            QString signalName = signal.attribute("name");
            if (signal.isNull() || signalName.isEmpty())
                continue;

            QDBusIntrospection::Signal signalData;
            signalData.name = signalName;

            // parse data
            signalData.outputArgs = parseArgs(signal, QLatin1String("out"), true);
            signalData.annotations = parseAnnotations(signal);

            // add it
            ifaceData->signals_.insert(signalName, signalData);
        }

        // parse properties
        list = iface.elementsByTagName("property");
        for (int j = 0; j < list.count(); ++j)
        {
            QDomElement property = list.item(j).toElement();
            QString propertyName = property.attribute("name");
            if (property.isNull() || propertyName.isEmpty())
                continue;

            QDBusIntrospection::Property propertyData;

            // parse data
            propertyData.name = propertyName;
            propertyData.type = parseType(property.attribute("type"));
            propertyData.annotations = parseAnnotations(property);

            if (!propertyData.type.isValid())
                // cannot be!
                continue;

            QString access = property.attribute("access");
            if (access.isEmpty())
                // can't be empty either!
                continue;
            else if (access == QLatin1String("read"))
                propertyData.access = QDBusIntrospection::Property::Read;
            else if (access == QLatin1String("write"))
                propertyData.access = QDBusIntrospection::Property::Write;
            else if (access == QLatin1String("readwrite"))
                propertyData.access = QDBusIntrospection::Property::ReadWrite;
            else
                continue;       // invalid one!

            // add it
            ifaceData->properties.insert(propertyName, propertyData);
        }

        // add it
        retval.insert(ifaceName, QSharedDataPointer<QDBusIntrospection::Interface>(ifaceData));
    }

    return retval;
}

QSharedDataPointer<QDBusIntrospection::Object>
QDBusXmlParser::object() const
{
    QSharedDataPointer<QDBusIntrospection::Object> retval;
    
    if (m_node.isNull())
        return retval;

    // check if the store knows about this one
    QDBusIntrospection::Object* objData;
    if (m_store) {
        retval = objData = m_store->findObject(m_service, m_path);
    }
    else {
        objData = new QDBusIntrospection::Object;
        objData->service = m_service;
        objData->path = m_path;
    }
    
    // check if we have anything to process
    if (objData->introspection.isNull() && !m_node.firstChild().isNull()) {
        // yes, introspect this object
        QTextStream ts(&objData->introspection);
        m_node.save(ts,2);
        
        QDomNodeList objects = m_node.elementsByTagName("node");
        for (int i = 0; i < objects.count(); ++i) {
            QDomElement obj = objects.item(i).toElement();
            QString objName = obj.attribute("name");
            if (obj.isNull() || objName.isEmpty())
                continue;           // for whatever reason

            objData->childObjects.append(objName);
        }

        QDomNodeList interfaces = m_node.elementsByTagName("interface");
        for (int i = 0; i < interfaces.count(); ++i) {
            QDomElement iface = interfaces.item(i).toElement();
            QString ifaceName = iface.attribute("name");
            if (iface.isNull() || ifaceName.isEmpty())
                continue;

            objData->interfaces.append(ifaceName);
        }
    }

    return retval;
}

QSharedDataPointer<QDBusIntrospection::ObjectTree>
QDBusXmlParser::objectTree() const
{
    QSharedDataPointer<QDBusIntrospection::ObjectTree> retval;

    if (m_node.isNull())
        return retval;

    retval = new QDBusIntrospection::ObjectTree;

    retval->service = m_service;
    retval->path = m_path;

    QTextStream ts(&retval->introspection);
    m_node.save(ts,2);    
    
    // interfaces are easy:
    retval->interfaceData = interfaces();
    retval->interfaces = retval->interfaceData.keys();

    // sub-objects are slightly more difficult:
    QDomNodeList objects = m_node.elementsByTagName("node");
    for (int i = 0; i < objects.count(); ++i) {
        QDomElement obj = objects.item(i).toElement();
        QString objName = obj.attribute("name");
        if (obj.isNull() || objName.isEmpty())
            continue;           // for whatever reason

        // check if we have anything to process
        if (!obj.firstChild().isNull()) {
            // yes, introspect this object
            QString xml;
            QTextStream ts(&xml);
            obj.save(ts,0);

            // parse it
            QString objAbsName = m_path;
            if (!objAbsName.endsWith('/'))
                objAbsName.append('/');
            objAbsName += objName;
            
            QDBusXmlParser parser(m_service, objAbsName, obj, m_store);
            retval->childObjectData.insert(objName, parser.objectTree());
        }

        retval->childObjects << objName;
    }

    return QSharedDataPointer<QDBusIntrospection::ObjectTree>( retval );
}
    

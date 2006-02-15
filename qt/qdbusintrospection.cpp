/* -*- C++ -*-
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "qdbusintrospection.h"
#include "qdbusxmlparser_p.h"

/*!
    \class QDBusIntrospection
    \brief Information about introspected objects and interfaces on D-Bus.

    This class provides structures and methods for parsing the XML introspection data for D-Bus.
    Normally, you don't have to use the methods provided here: QDBusInterface and QDBusObject will
    do that for you.

    But they may prove useful if the XML data was obtained through other means (like parsing a file).
*/

/*!
    \struct QDBusIntrospection::Argument
    \brief One argument to a D-Bus method or signal.

    This struct represents one argument passed to a method or received from a method or signal in
    D-Bus. The struct does not contain information on the direction (input or output).
*/

/*!
    \var QDBusIntrospection::Argument::type
    The argument type.
*/

/*!
    \var QDBusIntrospection::Argument::name
    The argument name. The argument name is optional, so this may be a null QString.
*/

/*!
    \struct QDBusIntrospection::Method
    \brief Information about one method.

    This struct represents one method discovered through introspection. A method is composed of
    its \a name, its input arguments, its output arguments, and, optionally, annotations. There are no
    "in-out" arguments.
*/

/*!
    \var QDBusIntrospection::Method::name
    The method's name.
*/

/*!
    \var QDBusIntrospection::Method::inputArgs
    A list of the method's input arguments.
*/

/*!
    \var QDBusIntrospection::Method::outputArgs
    A list of the method's output arguments (i.e., return values).
*/

/*!
    \var QDBusIntrospection::Method::annotations
    The annotations associated with the method. Each annotation is a pair of strings, where the key
    is of the same format as a D-Bus interface name. The value is arbitrary.
*/

/*!
    \struct QDBusIntrospection::Signal
    \brief Information about one signal.

    This struct represents one signal discovered through introspection. A signal is composed of
    its \a name, its output arguments, and, optionally, annotations.
*/

/*!
    \var QDBusIntrospection::Signal::outputArgs
    A list of the signal's arguments.
*/

/*!
    \var QDBusIntrospection::Signal::annotations
    The annotations associated with the signal. Each annotation is a pair of strings, where the key
    is of the same format as a D-Bus interface name. The value is arbitrary.
*/

/*!
    \struct QDBusIntrospection::Property
    \brief Information about one property.

    This struct represents one property discovered through introspection. A property is composed of
    its \a name, its \a type, its \a access rights, and, optionally, annotations.
*/

/*!
    \var QDBusIntrospection::Property::name
    The property's name.
*/

/*!
    \var QDBusIntrospection::Property::type
    The property's type.
*/

/*!
    \enum QDBusIntrospection::Property::Access
    The possible access rights for a property:
    - Read
    - Write
    - ReadWrite
*/

/*!
    \var QDBusIntrospection::Property::access
    The property's access rights.
*/

/*!
    \var QDBusIntrospection::Property::annotations
    The annotations associated with the property. Each annotation is a pair of strings, where the key
    is of the same format as a D-Bus interface name. The value is arbitrary.
*/

/*!
    \struct QDBusIntrospection::Interface
    \brief Information about one interface on the bus.

    Each interface on D-Bus has an unique \a name, identifying where that interface was defined.
    Interfaces may have annotations, methods, signals and properties, but none are mandatory.
*/

/*!
    \var QDBusIntrospection::Interface::name
    The interface's name.
*/

/*!
    \var QDBusIntrospection::Interface::introspection
    The XML document fragment describing this interface.

    If parsed again through parseInterface, the object returned should have the same contents as
    this object.
*/

/*!
    \var QDBusIntrospection::Interface::annotations
    The annotations associated with the interface. Each annotation is a pair of strings, where the key
    is of the same format as a D-Bus interface name. The value is arbitrary.
*/

/*!
    \var QDBusIntrospection::Interface::methods
    The methods available in this interface. Note that method names are not unique (i.e., methods
    can be overloaded with multiple arguments types).
*/

/*!
    \var QDBusIntrospection::Interface::signals_
    The signals available in this interface. Note that signal names are not unique (i.e., signals
    can be overloaded with multiple argument types).

    This member is called "signals_" because "signals" is a reserved keyword in Qt.
*/

/*!
    \var QDBusIntrospection::Interface::properties
    The properties available in this interface. Property names are unique.
*/

/*!
    \struct QDBusIntrospection::Object
    \brief Information about one object on the bus.

    An object on the D-Bus bus is represented by its service and path on the service but, unlike
    interfaces, objects are mutable. That is, their contents can change with time. Therefore,
    while the (service, path) pair uniquely identifies an object, the information contained in
    this struct may no longer represent the object.

    An object can contain interfaces and child (sub) objects.
*/

/*!
    \var QDBusIntrospection::Object::service
    The object's service name.

    \sa parseObject, parseObjectTree
*/

/*!
    \var QDBusIntrospection::Object::path
    The object's path on the service. This is an absolute path.

    \sa parseObject, parseObjectTree
*/

/*!
    \var QDBusIntrospection::Object::introspection
    The XML document fragment describing this object, its interfaces and sub-objects at the time
    of the parsing.

    The result of parseObject with this XML data should be the same as the Object struct.
*/

/*!
    \var QDBusIntrospection::Object::interfaces
    The list of interface names in this object.
*/

/*!
    \var QDBusIntrospection::Object::childObjects
    The list of child object names in this object. Note that this is a relative name, not an
    absolute path. To obtain the absolute path, concatenate with \ref path.
*/

/*!
    \struct QDBusIntrospection::ObjectTree
    \brief Complete information about one object node and its descendency.
    This struct contains the same data as QDBusIntrospection::Object, plus the actual data for the
    interfaces and child (sub) objects that was available in the XML document.
*/

/*!
    \var QDBusIntrospection::ObjectTree::interfaceData
    A map of interfaces and their names.
*/

/*!
    \var QDBusIntrospection::ObjectTree::childObjectData
    A map of object paths and their data. The map key contains the relative path to the object.

    Note this map contains only the child notes that do have information about the sub-object's
    contents. If the XML data did not contain the information, only the object name will be listed
    in childObjects, but not in childObjectData.
*/

/*!
    \typedef QDBusIntrospection::Annotations
    Contains a QMap of an annotation pair. The annotation's name is stored in the QMap key and
    must be unique. The annotation's value is stored in the QMap's value and is arbitrary.
*/

/*!
    \typedef QDBusIntrospection::Arguments
    Contains a list of arguments to either a Method or a Signal. The arguments' order is important.
*/

/*!
    \typedef QDBusIntrospection::Methods
    Contains a QMap of methods and their names. The method's name is stored in the map's key and
    is not necessarily unique. The order in which multiple methods with the same name are stored
    in this map is undefined.
*/

/*!
    \typedef QDBusIntrospection::Signals
    Contains a QMap of signals and their names. The signal's name is stored in the map's key and
    is not necessarily unique. The order in which multiple signals with the same name are stored
    in this map is undefined.
*/

/*!
    \typedef QDBusIntrospection::Properties
    Contains a QMap of properties and their names. Each property must have a unique name.
*/

/*!
    \typedef QDBusIntrospection::Interfaces
    Contains a QMap of interfaces and their names. Each interface has a unique name.
*/

/*!
    \typedef QDBusIntrospection::Objects
    Contains a QMap of objects and their paths relative to their immediate parent.

    \sa parseObjectTree
*/

/*!
    Parses the XML document fragment containing one interface.

    The first element tag in this XML data must be either <node> or <interface>. If it is
    <node>, then the <interface> tag must be a child tag of the <node> one.

    If there are multiple interfaces in this XML data, it is undefined which one will be
    returned.

    \param xml          the XML data to be parsed
    \returns            the parsed interface
*/
QDBusIntrospection::Interface
QDBusIntrospection::parseInterface(const QString &xml)
{
    // be lazy
    Interfaces ifs = parseInterfaces(xml);
    if (ifs.isEmpty())
        return Interface();

    // return the first in map order (probably alphabetical order)
    return *ifs.constBegin().value();
}

/*!
    Parses the XML document fragment containing several interfaces.

    If the first element tag in this document fragment is <node>, the interfaces parsed will
    be those found as child elements of the <node> tag.

    \param xml          the XML data to be parsed
    \returns            the parsed interfaces
*/
QDBusIntrospection::Interfaces
QDBusIntrospection::parseInterfaces(const QString &xml)
{
    QDBusXmlParser parser(QString(), QString(), xml);
    return parser.interfaces();
}

/*!
    Parses the XML document fragment containing one object.

    The first element tag in this document must be <node>. If that tag does not contain
    a name attribute, the \a path argument will be used to determine the path of this
    object node.

    This function does not parse the interfaces contained in the node, nor sub-object's contents.
    It will only list their names. If you need to know their contents, use parseObjectTree.

    \param xml          the XML data to be parsed
    \param service      the service where this object is found
    \param path         the absolute path to this node on the remote service
    \returns            the parsed object
*/
QDBusIntrospection::Object
QDBusIntrospection::parseObject(const QString &xml, const QString &service, const QString &path)
{
    QDBusXmlParser parser(service, path, xml);
    QSharedDataPointer<QDBusIntrospection::Object> retval = parser.object();
    if (!retval)
        return QDBusIntrospection::Object();
    return *retval;
}

/*!
    Parses the XML document fragment containing one object node and returns all the information
    about the interfaces and sub-objects.

    The Objects map returned will contain the absolute path names in the key.

    \param xml          the XML data to be parsed
    \param service      the service where this object is found
    \param path         the absolute path to this node on the remote service
    \returns            the parsed objects and interfaces
*/
QDBusIntrospection::ObjectTree
QDBusIntrospection::parseObjectTree(const QString &xml, const QString &service, const QString &path)
{
    QDBusXmlParser parser(service, path, xml);
    QSharedDataPointer<QDBusIntrospection::ObjectTree> retval = parser.objectTree();
    if (!retval)
        return QDBusIntrospection::ObjectTree();
    return *retval;
}

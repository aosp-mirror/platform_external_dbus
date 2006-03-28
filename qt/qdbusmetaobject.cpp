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

#include "qdbusmetaobject_p.h"

#include <QtCore/qbytearray.h>
#include <QtCore/qhash.h>
#include <QtCore/qstring.h>
#include <QtCore/qvarlengtharray.h>

#include "qdbusutil.h"
#include "qdbuserror.h"
#include "qdbusintrospection_p.h"
#include "qdbusabstractinterface_p.h"

class QDBusMetaObjectGenerator
{
public:
    QDBusMetaObjectGenerator(const QString &interface,
                             const QDBusIntrospection::Interface *parsedData);
    void write(QDBusMetaObject *obj);
    void writeWithoutXml(QDBusMetaObject *obj);

private:
    struct Method {
        QByteArray parameters;
        QByteArray typeName;
        QByteArray tag;
        QByteArray inputSignature;
        QByteArray outputSignature;
        QVarLengthArray<int, 6> inputTypes;
        QVarLengthArray<int, 2> outputTypes;
        int flags;
    };
    
    struct Property {
        QByteArray typeName;
        QByteArray signature;
        int type;
        int flags;
    };

    enum PropertyFlags  {
        Invalid = 0x00000000,
        Readable = 0x00000001,
        Writable = 0x00000002,
        Resettable = 0x00000004,
        EnumOrFlag = 0x00000008,
        StdCppSet = 0x00000100,
  //    Override = 0x00000200,
        Designable = 0x00001000,
        ResolveDesignable = 0x00002000,
        Scriptable = 0x00004000,
        ResolveScriptable = 0x00008000,
        Stored = 0x00010000,
        ResolveStored = 0x00020000,
        Editable = 0x00040000,
        ResolveEditable = 0x00080000,
        User = 0x00100000,
        ResolveUser = 0x00200000
    };

    enum MethodFlags  {
        AccessPrivate = 0x00,
        AccessProtected = 0x01,
        AccessPublic = 0x02,
        AccessMask = 0x03, //mask

        MethodMethod = 0x00,
        MethodSignal = 0x04,
        MethodSlot = 0x08,
        MethodTypeMask = 0x0c,

        MethodCompatibility = 0x10,
        MethodCloned = 0x20,
        MethodScriptable = 0x40
    };

    QMap<QByteArray, Method> methods;
    QMap<QByteArray, Property> properties;
    
    const QDBusIntrospection::Interface *data;
    QString interface;

    void parseMethods();
    void parseSignals();
    void parseProperties();
};

static const int intsPerProperty = 2;
static const int intsPerMethod = 4;

// ### from kernel/qmetaobject.cpp (Qt 4.1.2):
struct QDBusMetaObjectPrivate
{
    int revision;
    int className;
    int classInfoCount, classInfoData;
    int methodCount, methodData;
    int propertyCount, propertyData;
    int enumeratorCount, enumeratorData;
    
    // this is specific for QDBusMetaObject:
    int propertyDBusData;
    int methodDBusData;
};

QDBusMetaObjectGenerator::QDBusMetaObjectGenerator(const QString &interfaceName,
                                                   const QDBusIntrospection::Interface *parsedData)
    : data(parsedData), interface(interfaceName)
{
    if (data) {
        parseProperties();
        parseSignals();             // call parseSignals first so that slots override signals
        parseMethods();
    }
}

void QDBusMetaObjectGenerator::parseMethods()
{
    foreach (const QDBusIntrospection::Method &m, data->methods) {
        Method mm;

        QByteArray prototype = m.name.toLatin1();
        prototype += '(';

        bool ok = true;

        // build the input argument list
        foreach (const QDBusIntrospection::Argument &arg, m.inputArgs) {
            int typeId = QDBusUtil::signatureToType(arg.type);
            if (typeId == QVariant::Invalid) {
                ok = false;
                break;
            }

            mm.inputSignature += arg.type;
            mm.inputTypes.append(typeId);

            mm.parameters.append(arg.name.toLatin1());
            mm.parameters.append(',');
            
            prototype.append( QVariant::typeToName( QVariant::Type(typeId) ) );
            prototype.append(',');
        }
        if (!ok) continue;

        // build the output argument list:
        for (int i = 0; i < m.outputArgs.count(); ++i) {
            const QDBusIntrospection::Argument &arg = m.outputArgs.at(i);

            int typeId = QDBusUtil::signatureToType(arg.type);
            if (typeId == QVariant::Invalid) {
                ok = false;
                break;
            }

            mm.outputSignature += arg.type;
            mm.outputTypes.append(typeId);

            if (i == 0) {
                // return value
                mm.typeName = QVariant::typeToName( QVariant::Type(typeId) );
            } else {
                // non-const ref parameter
                mm.parameters.append(arg.name.toLatin1());
                mm.parameters.append(',');

                prototype.append( QVariant::typeToName( QVariant::Type(typeId) ) );
                prototype.append("&,");
            }
        }
        if (!ok) continue;

        // convert the last commas:
        if (!mm.parameters.isEmpty()) {
            mm.parameters.truncate(mm.parameters.length() - 1);
            prototype[prototype.length() - 1] = ')';
        } else {
            prototype.append(')');
        }

        // check the async tag
        if (m.annotations.value(QLatin1String(ANNOTATION_NO_WAIT)) == QLatin1String("true"))
            mm.tag = "Q_ASYNC";

        // meta method flags
        mm.flags = AccessPublic | MethodSlot | MethodScriptable;

        // add
        methods.insert(QMetaObject::normalizedSignature(prototype), mm);
    }
}

void QDBusMetaObjectGenerator::parseSignals()
{
    foreach (const QDBusIntrospection::Signal &s, data->signals_) {
        Method mm;

        QByteArray prototype = s.name.toLatin1();
        prototype += '(';

        bool ok = true;

        // build the output argument list
        foreach (const QDBusIntrospection::Argument &arg, s.outputArgs) {
            int typeId = QDBusUtil::signatureToType(arg.type);
            if (typeId == QVariant::Invalid) {
                ok = false;
                break;
            }

            mm.inputSignature += arg.type;
            mm.inputTypes.append(typeId);

            mm.parameters.append(arg.name.toLatin1());
            mm.parameters.append(',');
            
            prototype.append( QVariant::typeToName( QVariant::Type(typeId) ) );
            prototype.append(',');
        }
        if (!ok) continue;

        // convert the last commas:
        if (!mm.parameters.isEmpty()) {
            mm.parameters.truncate(mm.parameters.length() - 1);
            prototype[prototype.length() - 1] = ')';
        } else {
            prototype.append(')');
        }

        // meta method flags
        mm.flags = AccessProtected | MethodSignal | MethodScriptable;

        // add
        methods.insert(QMetaObject::normalizedSignature(prototype), mm);
    }
}

void QDBusMetaObjectGenerator::parseProperties()
{
    foreach (const QDBusIntrospection::Property &p, data->properties) {
        Property mp;
        mp.type = QDBusUtil::signatureToType(p.type);
        if (mp.type == QVariant::Invalid)
            continue;
        
        QByteArray name = p.name.toLatin1();
        mp.signature = p.type.toLatin1();
        mp.typeName = QVariant::typeToName( QVariant::Type(mp.type) );

        // build the flags:
        mp.flags = StdCppSet | Scriptable | Stored;
        if (p.access != QDBusIntrospection::Property::Write)
            mp.flags |= Readable;
        if (p.access != QDBusIntrospection::Property::Read)
            mp.flags |= Writable;

        if (mp.typeName == "QVariant")
            mp.flags |= 0xff << 24;
        else if (mp.type < 0xff)
            // encode the type in the flags
            mp.flags |= mp.type << 24;

        // add the property:
        properties.insert(name, mp);
    }
}

void QDBusMetaObjectGenerator::write(QDBusMetaObject *obj)
{
    // this code here is mostly copied from qaxbase.cpp
    // with a few modifications to make it cleaner
    
    QString className = interface;
    className.replace(QLatin1Char('.'), QLatin1String("::"));
    if (className.isEmpty())
        className = QLatin1String("QDBusInterface");

    QVarLengthArray<uint> data;
    data.resize(sizeof(QDBusMetaObjectPrivate) / sizeof(int));

    QDBusMetaObjectPrivate *header = reinterpret_cast<QDBusMetaObjectPrivate *>(data.data());
    header->revision = 1;
    header->className = 0;
    header->classInfoCount = 0;
    header->classInfoData = 0;
    header->methodCount = methods.count();
    header->methodData = data.size();
    header->propertyCount = properties.count();
    header->propertyData = header->methodData + header->methodCount * 5;
    header->enumeratorCount = 0;
    header->enumeratorData = 0;
    header->propertyDBusData = header->propertyData + header->propertyCount * 3;
    header->methodDBusData = header->propertyDBusData + header->propertyCount * intsPerProperty;

    int data_size = data.size() +
                    (header->methodCount * (5+intsPerMethod)) +
                    (header->propertyCount * (3+intsPerProperty));
    foreach (const Method &mm, methods)
        data_size += 2 + mm.inputTypes.count() + mm.outputTypes.count();
    data.resize(data_size + 1);

    char null('\0');
    QByteArray stringdata = className.toLatin1();
    stringdata += null;
    stringdata.reserve(8192);

    int offset = header->methodData;
    int signatureOffset = header->methodDBusData;
    int typeidOffset = header->methodDBusData + header->methodCount * intsPerMethod;
    data[typeidOffset++] = 0;                           // eod

    // add each method:
    for (QMap<QByteArray, Method>::ConstIterator it = methods.constBegin();
         it != methods.constEnd(); ++it) {
        // form "prototype\0parameters\0typeName\0tag\0inputSignature\0outputSignature"
        const Method &mm = it.value();

        data[offset++] = stringdata.length();
        stringdata += it.key();                 // prototype
        stringdata += null;
        data[offset++] = stringdata.length();
        stringdata += mm.parameters;
        stringdata += null;
        data[offset++] = stringdata.length();
        stringdata += mm.typeName;
        stringdata += null;
        data[offset++] = stringdata.length();
        stringdata += mm.tag;
        stringdata += null;
        data[offset++] = mm.flags;

        data[signatureOffset++] = stringdata.length();
        stringdata += mm.inputSignature;
        stringdata += null;
        data[signatureOffset++] = stringdata.length();
        stringdata += mm.outputSignature;
        stringdata += null;

        data[signatureOffset++] = typeidOffset;
        data[typeidOffset++] = mm.inputTypes.count();
        memcpy(data.data() + typeidOffset, mm.inputTypes.data(), mm.inputTypes.count() * sizeof(int));
        typeidOffset += mm.inputTypes.count();

        data[signatureOffset++] = typeidOffset;
        data[typeidOffset++] = mm.outputTypes.count();
        memcpy(data.data() + typeidOffset, mm.outputTypes.data(), mm.outputTypes.count() * sizeof(int));
        typeidOffset += mm.outputTypes.count();
    }

    Q_ASSERT(offset == header->propertyData);
    Q_ASSERT(signatureOffset == header->methodDBusData + header->methodCount * intsPerMethod);
    Q_ASSERT(typeidOffset == data.size());

    // add each property
    signatureOffset = header->propertyDBusData;
    for (QMap<QByteArray, Property>::ConstIterator it = properties.constBegin();
         it != properties.constEnd(); ++it) {
        const Property &mp = it.value();

        // form is "name\0typeName\0signature\0"
        data[offset++] = stringdata.length();
        stringdata += it.key();                 // name
        stringdata += null;
        data[offset++] = stringdata.length();
        stringdata += mp.typeName;
        stringdata += null;
        data[offset++] = mp.flags;

        data[signatureOffset++] = stringdata.length();
        stringdata += mp.signature;
        stringdata += null;
        data[signatureOffset++] = mp.type;
    }

    Q_ASSERT(offset == header->propertyDBusData);
    Q_ASSERT(signatureOffset == header->methodDBusData);

    char *string_data = new char[stringdata.length()];
    memcpy(string_data, stringdata, stringdata.length());

    uint *uint_data = new uint[data.size()];
    memcpy(uint_data, data.data(), data.size() * sizeof(int));

    // put the metaobject together
    obj->d.data = uint_data;
    obj->d.extradata = 0;
    obj->d.stringdata = string_data;
    obj->d.superdata = &QDBusAbstractInterface::staticMetaObject;
}

#if 0
void QDBusMetaObjectGenerator::writeWithoutXml(const QString &interface)
{
    // no XML definition
    QString tmp(interface);
    tmp.replace(QLatin1Char('.'), QLatin1String("::"));
    QByteArray name(tmp.toLatin1());

    QDBusMetaObjectPrivate *header = new QDBusMetaObjectPrivate;
    memset(header, 0, sizeof *header);
    header->revision = 1;
    // leave the rest with 0

    char *stringdata = new char[name.length() + 1];
    stringdata[name.length()] = '\0';
    
    d.data = reinterpret_cast<uint*>(header);
    d.extradata = 0;
    d.stringdata = stringdata;
    d.superdata = &QDBusAbstractInterface::staticMetaObject;
    cached = false;
}
#endif

/////////
// class QDBusMetaObject

QDBusMetaObject *QDBusMetaObject::createMetaObject(QString &interface, const QString &xml,
                                                   QHash<QString, QDBusMetaObject *> &cache,
                                                   QDBusError &error)
{
    error = QDBusError();
    QDBusIntrospection::Interfaces parsed = QDBusIntrospection::parseInterfaces(xml);

    QDBusMetaObject *we = 0;
    QDBusIntrospection::Interfaces::ConstIterator it = parsed.constBegin();
    QDBusIntrospection::Interfaces::ConstIterator end = parsed.constEnd();
    for ( ; it != end; ++it) {
        // check if it's in the cache
        QDBusMetaObject *obj = cache.value(it.key(), 0);
        if (!obj) {
            // not in cache; create
            obj = new QDBusMetaObject;
            QDBusMetaObjectGenerator generator(it.key(), it.value().constData());
            generator.write(obj);

            if ( (obj->cached = !it.key().startsWith( QLatin1String("local.") )) )
                // cache it
                cache.insert(it.key(), obj);

        }

        if (it.key() == interface) {
            // it's us
            we = obj;
        } else if (interface.isEmpty() &&
                 !it.key().startsWith(QLatin1String("org.freedesktop.DBus."))) {
            // also us
            we = obj;
            interface = it.key();
        }
    }

    if (we)
        return we;
    // still nothing?
    
    if (parsed.isEmpty()) {
        // object didn't return introspection
        we = new QDBusMetaObject;
        QDBusMetaObjectGenerator generator(interface, 0);
        generator.write(we);
        we->cached = false;
        return we;
    }

    // mark as an error
    error = QDBusError(QDBusError::UnknownInterface,
                       QString( QLatin1String("Interface '%1' was not found") )
                       .arg(interface));
    return 0;
}

QDBusMetaObject::QDBusMetaObject()
{
}

inline const QDBusMetaObjectPrivate *priv(const uint* data)
{
    return reinterpret_cast<const QDBusMetaObjectPrivate *>(data);
}

const char *QDBusMetaObject::dbusNameForMethod(int id) const
{
    //id -= methodOffset();
    if (id >= 0 && id < priv(d.data)->methodCount) {
        int handle = priv(d.data)->methodDBusData + id*intsPerMethod;
        return d.stringdata + d.data[handle];
    }
    return 0;
}

const char *QDBusMetaObject::inputSignatureForMethod(int id) const
{
    //id -= methodOffset();
    if (id >= 0 && id < priv(d.data)->methodCount) {
        int handle = priv(d.data)->methodDBusData + id*intsPerMethod;
        return d.stringdata + d.data[handle + 1];
    }
    return 0;
}

const char *QDBusMetaObject::outputSignatureForMethod(int id) const
{
    //id -= methodOffset();
    if (id >= 0 && id < priv(d.data)->methodCount) {
        int handle = priv(d.data)->methodDBusData + id*intsPerMethod;
        return d.stringdata + d.data[handle + 2];
    }
    return 0;
}

const int *QDBusMetaObject::inputTypesForMethod(int id) const
{
    //id -= methodOffset();
    if (id >= 0 && id < priv(d.data)->methodCount) {
        int handle = priv(d.data)->methodDBusData + id*intsPerMethod;
        return reinterpret_cast<const int*>(d.data + d.data[handle + 3]);
    }
    return 0;
}

const int *QDBusMetaObject::outputTypesForMethod(int id) const
{
    //id -= methodOffset();
    if (id >= 0 && id < priv(d.data)->methodCount) {
        int handle = priv(d.data)->methodDBusData + id*intsPerMethod;
        return reinterpret_cast<const int*>(d.data + d.data[handle + 4]);
    }
    return 0;
}

int QDBusMetaObject::propertyMetaType(int id) const
{
    //id -= propertyOffset();
    if (id >= 0 && id < priv(d.data)->propertyCount) {
        int handle = priv(d.data)->propertyDBusData + id*intsPerProperty;
        return d.data[handle + 1];
    }
    return 0;
}

template<typename T>
static inline void assign_helper(void *ptr, const QVariant &value)
{
    *reinterpret_cast<T *>(ptr) = qvariant_cast<T>(value);
}

void QDBusMetaObject::assign(void *ptr, const QVariant &value)
{
    switch (value.userType())
    {
    case QVariant::Bool:
        assign_helper<bool>(ptr, value);
        return;

    case QMetaType::UChar:
        assign_helper<uchar>(ptr, value);
        return;

    case QMetaType::Short:
        assign_helper<short>(ptr, value);
        return;

    case QMetaType::UShort:
        assign_helper<ushort>(ptr, value);
        return;

    case QVariant::Int:
        assign_helper<int>(ptr, value);
        return;

    case QVariant::UInt:
        assign_helper<uint>(ptr, value);
        return;

    case QVariant::LongLong:
        assign_helper<qlonglong>(ptr, value);
        return;

    case QVariant::ULongLong:
        assign_helper<qulonglong>(ptr, value);
        return;

    case QVariant::Double:
        assign_helper<double>(ptr, value);
        return;

    case QVariant::String:
        assign_helper<QString>(ptr, value);
        return;

    case QVariant::ByteArray:
        assign_helper<QByteArray>(ptr, value);
        return;

    case QVariant::List:
        assign_helper<QVariantList>(ptr, value);
        return;

    case QVariant::Map:
        assign_helper<QVariantMap>(ptr, value);
        return;

    default:
        ;    
    }
}

bool QDBusMetaTypeId::initialized = false;
int QDBusMetaTypeId::variant = 0;
int QDBusMetaTypeId::boollist = 0;
int QDBusMetaTypeId::shortlist = 0;
int QDBusMetaTypeId::ushortlist = 0;
int QDBusMetaTypeId::intlist = 0;
int QDBusMetaTypeId::uintlist = 0;
int QDBusMetaTypeId::longlonglist = 0;
int QDBusMetaTypeId::ulonglonglist = 0;
int QDBusMetaTypeId::doublelist = 0;

bool QDBusMetaTypeId::innerInitialize()
{
    variant = qRegisterMetaType<QVariant>("QVariant");
    boollist = qRegisterMetaType<QList<bool> >("QList<bool>");
    shortlist = qRegisterMetaType<QList<short> >("QList<short>");
    ushortlist = qRegisterMetaType<QList<ushort> >("QList<ushort>");
    intlist = qRegisterMetaType<QList<int> >("QList<int>");
    uintlist = qRegisterMetaType<QList<uint> >("QList<uint>");
    longlonglist = qRegisterMetaType<QList<qlonglong> >("QList<qlonglong>");
    ulonglonglist = qRegisterMetaType<QList<qulonglong> >("QList<qulonglong>");
    doublelist = qRegisterMetaType<QList<double> >("QList<double>");
    initialized = true;
    return true;
}

int qDBusMetaTypeId(QVariant *)
{ QDBusMetaTypeId::initialize(); return QDBusMetaTypeId::variant; }
int qDBusMetaTypeId(QList<bool> *)
{ QDBusMetaTypeId::initialize(); return QDBusMetaTypeId::boollist; }
int qDBusMetaTypeId(QList<short> *)
{ QDBusMetaTypeId::initialize(); return QDBusMetaTypeId::shortlist; }
int qDBusMetaTypeId(QList<ushort> *)
{ QDBusMetaTypeId::initialize(); return QDBusMetaTypeId::ushortlist; }
int qDBusMetaTypeId(QList<int> *)
{ QDBusMetaTypeId::initialize(); return QDBusMetaTypeId::intlist; }
int qDBusMetaTypeId(QList<uint> *)
{ QDBusMetaTypeId::initialize(); return QDBusMetaTypeId::uintlist; }
int qDBusMetaTypeId(QList<qlonglong> *)
{ QDBusMetaTypeId::initialize(); return QDBusMetaTypeId::longlonglist; }
int qDBusMetaTypeId(QList<qulonglong> *)
{ QDBusMetaTypeId::initialize(); return QDBusMetaTypeId::ulonglonglist; }
int qDBusMetaTypeId(QList<double> *)
{ QDBusMetaTypeId::initialize(); return QDBusMetaTypeId::doublelist; }

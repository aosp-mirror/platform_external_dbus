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

#include "qdbustype.h"
#include "qdbusvariant.h"
#include <dbus/dbus.h>

#include <QtCore/qstringlist.h>

/// \internal
class QDBusPrettyTypeBase
{
public:
    struct Entry
    {
        const char* prettyName;
        char signature;
    };

    enum Direction
    {
        In,
        Out
    };

    enum Access
    {
        Read,
        Write,
        ReadWrite
    };

    // so that the compiler doesn't complain
    virtual ~QDBusPrettyTypeBase() { }

    virtual QString addElementsToArray(const QString& subType) = 0;
    virtual QString addElementsToMap(const QString& key, const QString& value) = 0;
    virtual QString addElementsToStruct(const QStringList& subTypes) = 0;
    virtual const Entry* entryMap() = 0;

    QString toString(const QDBusType& type);
    QString toString(const QDBusTypeList& list);
};

/// \internal
class QDBusConventionalNames: public QDBusPrettyTypeBase
{
public:
    virtual QString addElementsToArray(const QString& subType);
    virtual QString addElementsToMap(const QString& key, const QString& value);
    virtual QString addElementsToStruct(const QStringList& subTypes) ;
    virtual const Entry* entryMap();
};

/// \internal
class QDBusQtNames: public QDBusPrettyTypeBase
{
public:
    virtual QString addElementsToArray(const QString& subType);
    virtual QString addElementsToMap(const QString& key, const QString& value);
    virtual QString addElementsToStruct(const QStringList& subTypes) ;
    virtual const Entry* entryMap();
};

//! \internal
class QDBusQVariantNames: public QDBusQtNames
{
public:
    virtual QString addElementsToArray(const QString& subType);
    virtual QString addElementsToMap(const QString& key, const QString& value);
    virtual QString addElementsToStruct(const QStringList& subTypes) ;
};

static QString findInMap(char type, const QDBusPrettyTypeBase::Entry* map)
{
    for ( ; map->signature; ++map)
        if (type == map->signature)
            return QLatin1String(map->prettyName);
    return QString();
}

//
// Input MUST be valid
//
inline QString QDBusPrettyTypeBase::toString(const QDBusType& type)
{
    const Entry* map = entryMap();

    const QDBusTypeList subTypes = type.subTypes();
    switch (type.dbusType()) {
    case DBUS_TYPE_STRUCT: {
        // handle a struct
        // find its sub-types

        QStringList subStrings;
        QDBusTypeList subTypes = type.subTypes();
        foreach (QDBusType t, subTypes)
            subStrings << toString( t );

        return addElementsToStruct(subStrings);
    }

    case DBUS_TYPE_DICT_ENTRY: {
        Q_ASSERT_X(subTypes.size() == 2, "QDBusType::toString",
                   "maps must have exactly two elements");

        QString key = findInMap( subTypes.at(0).dbusType(), map );
        QString value = toString( subTypes.at(1) );

        Q_ASSERT(!key.isNull());

        return addElementsToMap( key, value );
    }
    case DBUS_TYPE_ARRAY: {
        Q_ASSERT_X(subTypes.size() == 1, "QDBusType::toString",
                   "more than one element in array");

        if (type.qvariantType() == QVariant::Map)
            return toString( subTypes.first() );
        return addElementsToArray( toString( subTypes.at(0) ) );
    }

    default: {
        // normal, non-compound type
        QString name = findInMap(type.dbusType(), map);
        Q_ASSERT(!name.isNull());
        return name;
    }
    }
}

const QDBusPrettyTypeBase::Entry* QDBusConventionalNames::entryMap()
{
    static QDBusPrettyTypeBase::Entry translation[] = {
        { "BYTE", DBUS_TYPE_BYTE },
        { "BOOLEAN", DBUS_TYPE_BOOLEAN },
        { "INT16", DBUS_TYPE_INT16 },
        { "UINT16", DBUS_TYPE_UINT16 },
        { "INT32", DBUS_TYPE_INT32 },
        { "UINT32", DBUS_TYPE_UINT32 },
        { "INT64", DBUS_TYPE_INT64 },
        { "UINT64", DBUS_TYPE_UINT64 },
        { "DOUBLE", DBUS_TYPE_DOUBLE },
        { "STRING", DBUS_TYPE_STRING },
        { "OBJECT_PATH", DBUS_TYPE_OBJECT_PATH },
        { "SIGNATURE", DBUS_TYPE_SIGNATURE },
        { "VARIANT", DBUS_TYPE_VARIANT }
    };
    return translation;
}

QString QDBusConventionalNames::addElementsToStruct(const QStringList& subTypes)
{
    return QString( QLatin1String("STRUCT of (%1)") )
        .arg( subTypes.join( QLatin1String(",") ) );
}

QString QDBusConventionalNames::addElementsToMap(const QString& key, const QString& value)
{
    return QString( QLatin1String("ARRAY of DICT_ENTRY of (%1,%2)") )
        .arg(key).arg(value);
}

QString QDBusConventionalNames::addElementsToArray(const QString& subType)
{
    return QString( QLatin1String("ARRAY of %1") )
        .arg(subType);
}

const QDBusPrettyTypeBase::Entry* QDBusQtNames::entryMap()
{
    static QDBusPrettyTypeBase::Entry translation[] = {
        { "uchar", DBUS_TYPE_BYTE },
        { "bool", DBUS_TYPE_BOOLEAN },
        { "short", DBUS_TYPE_INT16 },
        { "ushort", DBUS_TYPE_UINT16 },
        { "int", DBUS_TYPE_INT32 },
        { "uint", DBUS_TYPE_UINT32 },
        { "qlonglong", DBUS_TYPE_INT64 },
        { "qulonglong", DBUS_TYPE_UINT64 },
        { "double", DBUS_TYPE_DOUBLE },
        { "QString", DBUS_TYPE_STRING },
        { "QString", DBUS_TYPE_OBJECT_PATH },
        { "QString", DBUS_TYPE_SIGNATURE },
        { "QDBusVariant", DBUS_TYPE_VARIANT }
    };
    return translation;
}

static inline QString templateArg(const QString& input)
{
    if (input.endsWith(QLatin1Char('>')))
        return input + QLatin1Char(' ');
    return input;
}

QString QDBusQtNames::addElementsToStruct(const QStringList& subTypes)
{
    Q_UNUSED(subTypes);

    return QLatin1String("QVariantList");      // CHANGEME in the future
}

QString QDBusQtNames::addElementsToMap(const QString& key, const QString& value)
{
    if (key == QLatin1String("QString") && value == QLatin1String("QDBusVariant"))
        return QLatin1String("QVariantMap");

    return QString( QLatin1String("QMap<%1, %2>") )
        .arg(key)
        .arg( templateArg(value) );
}

QString QDBusQtNames::addElementsToArray(const QString& subType)
{
    if (subType == QLatin1String("uchar"))
        // special case
        return QLatin1String("QByteArray");
    else if (subType == QLatin1String("QString"))
        // special case
        return QLatin1String("QStringList");

    return QString( QLatin1String("QList<%1>") )
        .arg( templateArg(subType) );
}

QString QDBusQVariantNames::addElementsToStruct(const QStringList& subTypes)
{
    Q_UNUSED(subTypes);

    return QLatin1String("QVariantList");
}

QString QDBusQVariantNames::addElementsToMap(const QString& key, const QString& value)
{
    Q_UNUSED(key);
    Q_UNUSED(value);

    return QLatin1String("QVariantMap");
}

QString QDBusQVariantNames::addElementsToArray(const QString& subType)
{
    if (subType == QLatin1String("uchar"))
        // special case
        return QLatin1String("QByteArray");
    else if (subType == QLatin1String("QString"))
        // special case
        return QLatin1String("QStringList");

    return QLatin1String("QVariantList");
}

/*!
    \internal
*/
class QDBusTypePrivate: public QSharedData
{
public:
    int code;
    mutable QVariant::Type qvariantType;
    mutable QByteArray signature;
    QDBusTypeList subTypes;

    inline QDBusTypePrivate()
        : code(0), qvariantType(QVariant::Invalid)
    { }
};

/*!
    \class QDBusType
    \brief Represents one single D-Bus type.

    D-Bus provides a set of primitive types that map to normal, C++ types and to QString, as well as
    the possibility to extend the set with the so-called "container" types. The available types are
    as follows:

    - Primitive (or basic): integers of 16, 32 and 64 bits, both signed and unsigned; byte (8 bits);
      double-precision floating point and Unicode strings
    - Arrays: a homogeneous, ordered list of zero or more entries
    - Maps: an unordered list of (key, value) pairs, where key must be a primitive type and value
      can be any D-Bus type
    - Structs: an ordered list of a fixed number of entries of any type
    - Variants: a "wildcard" container that can assume the value of any other type, including
      structs and arrays

    Any type can be placed inside an array (including other arrays), but only entries of the same
    type can be placed inside the same array. The analogous type for D-Bus arrays are the Qt
    #QList template classes.

    Structs have a fixed number of entries and each entry has a fixed type. They are analogous to C
    and C++ structs (hence the name).

    Maps or dictionaries are analogous to the Qt #QMap template class, with the additional
    restriction that the key type must be a primitive one. D-Bus implements maps by using arrays of
    a special type (a "dictionary entry"), so inspecting a QDBusType of a Map will reveal that it is
    an array (see isArray()).

    Variants contain exactly one entry, but the type can vary freely. It is analogous to the Qt
    class #QVariant, but the QtDBus implementation uses #QDBusVariant to represent D-Bus Variants.
*/

/*!
    \enum QDBusType::StringFormat

    This enum is used in QDBusType::toString to determine which type of formatting
    to apply to the D-Bus types:

    \value ConventionalNames    Use the DBus conventional names, such as STRING, BOOLEAN or
                                ARRAY of BYTE.
    \value QtNames              Use the Qt type names, such as QString, bool and QList<quint32>
    \value QVariantNames        Same as QtNames, but for containers, use QVariantList and QVariantMap
*/

/*!
    Constructs an empty (invalid) type.
*/
QDBusType::QDBusType()
    : d(0)
{
}

/*!
    Constructs the type based on the given DBus type.
*/
QDBusType::QDBusType(int type)
{
    char c[2] = { type, 0 };
    *this = QDBusType(c);
}

/*!
    Constructs the type based on the given QVariant type.

    \sa QVariant::Type
*/
QDBusType::QDBusType(QVariant::Type type)
{
    const char *sig = dbusSignature(type);

    // it never returns NULL
    if (sig[0] == '\0')
        return;

    d = new QDBusTypePrivate;
    d->qvariantType = type;
    d->code = sig[0];
    if (sig[1] == '\0')
        // single-letter type
        return;
    else if (sig[2] == '\0') {
        // two-letter type
        // must be an array
        d->code = sig[0];
        QDBusType t;
        t.d = new QDBusTypePrivate;
        t.d->code = sig[1];
        d->subTypes << t;
    }
    else {
        // the only longer type is "a{sv}"
        Q_ASSERT(sig[1] == '{' && sig[5] == '\0');

        static QDBusType map("a{sv}");
        d->subTypes = map.d->subTypes;
    }
}

/*!
    Parses the given DBus signature and constructs the type it represents.

    \param signature    the signature to parse. It must represent one single type, but can be
                        a container type.
*/
QDBusType::QDBusType(const char* signature)
{
    if ( !dbus_signature_validate_single(signature, 0) )
        return;

    DBusSignatureIter iter;
    dbus_signature_iter_init(&iter, signature);
    *this = QDBusType(&iter);
    if (d)
        d->signature = signature;
}

/*!
    Parses the given DBus signature and constructs the type it represents.

    \param str          the signature to parse. It must represent one single type, but can
                        a container type.
*/
QDBusType::QDBusType(const QString& str)
{
    *this = QDBusType( str.toUtf8().constData() );
}

/*!
    Parses the given DBus signature and constructs the type it represents.

    \param str          the signature to parse. It must represent one single type, but can
                        a container type.
*/
QDBusType::QDBusType(const QByteArray& str)
{
    *this = QDBusType( str.constData() );
}

/*!
    \internal
    Creates a QDBusType object based on the current element pointed to by \a iter.

    \param iter         the iterator. Can be pointing to container types.
*/
QDBusType::QDBusType(DBusSignatureIter* iter)
    : d(new QDBusTypePrivate)
{
    if ( dbus_type_is_container( d->code = dbus_signature_iter_get_current_type(iter) ) ) {
        // we have to recurse
        if ( d->code == DBUS_TYPE_VARIANT )
            return;             // no we don't. dbus_type_is_container lies to us

        // we have to recurse
        DBusSignatureIter subiter;
        dbus_signature_iter_recurse(iter, &subiter);

        d->subTypes = QDBusTypeList(&subiter);

        // sanity checking:
        if ( d->code == DBUS_TYPE_ARRAY )
            Q_ASSERT_X(d->subTypes.size() == 1, "QDBusType",
                       "more than one element in array");
        else if (d->code == DBUS_TYPE_DICT_ENTRY )
            Q_ASSERT_X(d->subTypes.size() == 2, "QDBusType",
                       "maps must have exactly two elements");
    }
}

/*!
    Copies the type from the other object.
*/
QDBusType::QDBusType(const QDBusType& other)
    : d(other.d)
{
}

/*!
    Release the resources associated with this type.
*/
QDBusType::~QDBusType()
{
}

/*!
    Copies the type from the other object.
*/
QDBusType& QDBusType::operator=(const QDBusType& other)
{
    d = other.d;
    return *this;
}

/*!
    Returns the DBus type for this type.
*/
int QDBusType::dbusType() const
{
    return d ? d->code : DBUS_TYPE_INVALID;
}

/*!
    Returns the DBus signature for this type and subtypes.
*/
QByteArray QDBusType::dbusSignature() const
{
    if (!d)
        return QByteArray();

    if (!d->signature.isEmpty())
        return d->signature;

    if (d->subTypes.isEmpty())
        return d->signature = QByteArray(1, d->code);

    QByteArray retval;
    switch (d->code) {
        // can only be array, map or struct

    case DBUS_TYPE_ARRAY:
        Q_ASSERT_X(d->subTypes.size() == 1, "QDBusType::dbusSignature",
                   "more than one element in array");

        retval += DBUS_TYPE_ARRAY;
        retval += d->subTypes.at(0).dbusSignature();
        break;

    case DBUS_TYPE_DICT_ENTRY: {
        Q_ASSERT_X(d->subTypes.size() == 2, "QDBusType::dbusSignature",
                   "maps must have exactly two elements");

        QByteArray value = d->subTypes.at(1).dbusSignature();
        char key = d->subTypes.at(0).dbusType();

        Q_ASSERT(key != DBUS_TYPE_INVALID);
        Q_ASSERT(!value.isEmpty());

        retval.reserve(value.length() + 3);
        retval  = DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING;
        retval += key;
        retval += value;
        retval += DBUS_DICT_ENTRY_END_CHAR;
        break;
    }

    case DBUS_TYPE_STRUCT:
        retval = d->subTypes.dbusSignature();
        retval.prepend(DBUS_STRUCT_BEGIN_CHAR);
        retval.append(DBUS_STRUCT_END_CHAR);
        break;

    default:
        Q_ASSERT_X(false, "QDBusType::dbusSignature", "invalid container type");
    }

    d->signature = retval;
    return retval;
}

/*!
    Returns the QVariant::Type for this entry.
*/
QVariant::Type QDBusType::qvariantType() const
{
    if (d && d->qvariantType != QVariant::Invalid)
        return d->qvariantType;

    // check the special array cases:
    if (isArray()) {
        QDBusType t = arrayElement();

        if (t.dbusType() == DBUS_TYPE_BYTE)
            return QVariant::ByteArray;
        else if (t.dbusType() == DBUS_TYPE_DICT_ENTRY)
            return QVariant::Map;
        else if (t.isBasic() && t.qvariantType() == QVariant::String)
            return QVariant::StringList;
    }

    return qvariantType(dbusType());
}

/*!
    Returns true if this type is a valid one.
*/
bool QDBusType::isValid() const
{
    return d && d->code != DBUS_TYPE_INVALID;
}

/*!
    Returns true if this type is a basic one.

    \sa dbus_type_is_basic
*/
bool QDBusType::isBasic() const
{
    return d && dbus_type_is_basic(d->code);
}

/*!
    Returns true if this type is a container.

    \sa dbus_type_is_container
*/
bool QDBusType::isContainer() const
{
    return d && dbus_type_is_container(d->code);
}

/*!
    Returns the subtypes of this type, if this is a container.

    \sa isContainer
*/
QDBusTypeList QDBusType::subTypes() const
{
    if (d)
        return d->subTypes;
    return QDBusTypeList();
}

/*!
    Returns true if this type is an array.

    \sa isContainer, arrayElement
*/
bool QDBusType::isArray() const
{
    return dbusType() == DBUS_TYPE_ARRAY;
}

/*!
    This is a convenience function that returns the element type of an array.
    If this object is not an array, it returns an invalid QDBusType.

    \sa isArray
*/
QDBusType QDBusType::arrayElement() const
{
    if (isArray() && d->subTypes.count() == 1)
        return d->subTypes.first();
    return QDBusType();
}

/*!
    Returns true if this type is a map (i.e., an array of dictionary entries).

    \sa isContainer, isArray, arrayElement
*/
bool QDBusType::isMap() const
{
    return arrayElement().dbusType() == DBUS_TYPE_DICT_ENTRY;
}

/*!
    If this object is a map, returns the (basic) type that corresponds to the key type.
    If this object is not a map, returns an invalid QDBusType.

    \sa isMap
*/
QDBusType QDBusType::mapKey() const
{
    if (isMap())
        return arrayElement().d->subTypes.first();
    return QDBusType();
}

/*!
    If this object is a map, returns the type that corresponds to the value type.
    If this object is not a map, returns an invalid QDBusType.

    \sa isMap
*/
QDBusType QDBusType::mapValue() const
{
    if (isMap())
        return arrayElement().d->subTypes.at(1);
    return QDBusType();
}

/*!
    Returns true if the two types match.
*/
bool QDBusType::operator==(const QDBusType& other) const
{
    if (!d && !other.d)
        return true;
    if (!d || !other.d)
        return false;
    return d->code == other.d->code && d->subTypes == other.d->subTypes;
}

/*!
    Returns a string representation of this type.
*/
QString QDBusType::toString(StringFormat sf) const
{
    switch (sf) {
    case ConventionalNames:
        return QDBusConventionalNames().toString(*this);

    case QtNames:
        return QDBusQtNames().toString(*this);

    case QVariantNames:
        return QDBusQVariantNames().toString(*this);
    }

    return QString();           // invalid
}

/*!
    Converts the DBus type to QVariant::Type
*/
QVariant::Type QDBusType::qvariantType(int type)
{
    char c[2] = { type, 0 };
    return qvariantType(c);
}

/*!
    Converts the DBus type signature to QVariant::Type.
*/
QVariant::Type QDBusType::qvariantType(const char* signature)
{
    if (!signature)
        return QVariant::Invalid;

    // three special cases that don't validate as single:
    if (qstrlen(signature) == 1) {
        if (signature[0] == DBUS_TYPE_STRUCT)
            return QVariant::List;
        else if (signature[0] == DBUS_TYPE_DICT_ENTRY)
            return QVariant::Map;
        else if (signature[0] == DBUS_TYPE_ARRAY)
            return QVariant::List;
    }

    // now we can validate
    if ( !dbus_signature_validate_single(signature, 0) )
        return QVariant::Invalid;

    switch (signature[0])
    {
    case DBUS_TYPE_BOOLEAN:
        return QVariant::Bool;

    case DBUS_TYPE_INT16:
    case DBUS_TYPE_INT32:
        return QVariant::Int;

    case DBUS_TYPE_BYTE:
    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_UINT32:
        return QVariant::UInt;

    case DBUS_TYPE_INT64:
        return QVariant::LongLong;

    case DBUS_TYPE_UINT64:
        return QVariant::ULongLong;

    case DBUS_TYPE_DOUBLE:
        return QVariant::Double;

    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE:
        return QVariant::String;

    case DBUS_STRUCT_BEGIN_CHAR:
        return QVariant::List;  // change to QDBusStruct in the future

    case DBUS_TYPE_VARIANT:
        return QVariant::UserType; // must set user-type too

    case DBUS_TYPE_ARRAY:       // special case
        // check if it's a string list
        if (qvariantType(signature + 1) == QVariant::String)
            return QVariant::StringList;

        // maybe it's a byte array
        if (DBUS_TYPE_BYTE == signature[1])
            return QVariant::ByteArray;

        // check if it's a dict
        if (DBUS_DICT_ENTRY_BEGIN_CHAR == signature[1])
            return QVariant::Map;

        return QVariant::List;

    default:
        return QVariant::Invalid;

    }
}

/*!
    Converts the QVariant::Type to a DBus type code.

    \param t            the type to convert
*/
int QDBusType::dbusType(QVariant::Type t)
{
    switch (t)
    {
    case QVariant::Bool:
        return DBUS_TYPE_BOOLEAN;

    case QVariant::Int:
        return DBUS_TYPE_INT32;

    case QVariant::UInt:
    case QVariant::Char:
        return DBUS_TYPE_UINT32;

    case QVariant::LongLong:
        return DBUS_TYPE_INT64;

    case QVariant::ULongLong:
        return DBUS_TYPE_UINT64;

    case QVariant::Double:
        return DBUS_TYPE_DOUBLE;

    // from QMetaType:
    case QMetaType::Short:
        return DBUS_TYPE_INT16;

    case QMetaType::UShort:
        return DBUS_TYPE_UINT16;

    case QMetaType::UChar:
        return DBUS_TYPE_BYTE;

    case QVariant::String:
    case QVariant::Date:
    case QVariant::Time:
    case QVariant::DateTime:
        return DBUS_TYPE_STRING;

    case QVariant::Map:
        // internal type information has been lost
        return DBUS_TYPE_DICT_ENTRY;

    case QVariant::List:
    case QVariant::StringList:
    case QVariant::ByteArray:
        // could also be a struct...
        return DBUS_TYPE_ARRAY;

    case QVariant::UserType:
        return DBUS_TYPE_VARIANT;

    default:
        break;                  // avoid compiler warnings
    }

    if (int(t) == QMetaTypeId<QDBusVariant>::qt_metatype_id())
        return DBUS_TYPE_VARIANT;

    return DBUS_TYPE_INVALID;
}

/*!
    Converts the QVariant::Type to a DBus type signature.

    \param t            the type to convert
*/
const char* QDBusType::dbusSignature(QVariant::Type t)
{
    switch (t)
    {
    case QVariant::Bool:
        return DBUS_TYPE_BOOLEAN_AS_STRING;

    case QVariant::Int:
        return DBUS_TYPE_INT32_AS_STRING;

    case QVariant::UInt:
    case QVariant::Char:
        return DBUS_TYPE_UINT32_AS_STRING;

    case QMetaType::Short:
        return DBUS_TYPE_INT16_AS_STRING;

    case QMetaType::UShort:
        return DBUS_TYPE_UINT16_AS_STRING;

    case QMetaType::UChar:
        return DBUS_TYPE_BYTE_AS_STRING;

    case QVariant::LongLong:
        return DBUS_TYPE_INT64_AS_STRING;

    case QVariant::ULongLong:
        return DBUS_TYPE_UINT64_AS_STRING;

    case QVariant::Double:
        return DBUS_TYPE_DOUBLE_AS_STRING;

    case QVariant::String:
    case QVariant::Date:
    case QVariant::Time:
    case QVariant::DateTime:
        return DBUS_TYPE_STRING_AS_STRING;

    case QVariant::Map:
        // internal type information has been lost
        return DBUS_TYPE_ARRAY_AS_STRING
            DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
            DBUS_TYPE_STRING_AS_STRING
            DBUS_TYPE_VARIANT_AS_STRING
            DBUS_DICT_ENTRY_END_CHAR_AS_STRING; // a{sv}

    case QVariant::StringList:
        return DBUS_TYPE_ARRAY_AS_STRING
            DBUS_TYPE_STRING_AS_STRING; // as

    case QVariant::ByteArray:
        return DBUS_TYPE_ARRAY_AS_STRING
            DBUS_TYPE_BYTE_AS_STRING; // ay

    case QVariant::List:
        // not a string list
        // internal list data has been lost
        // could also be a struct...
        return DBUS_TYPE_ARRAY_AS_STRING
            DBUS_TYPE_VARIANT_AS_STRING; // av

    default:
        if (int(t) == qMetaTypeId<QDBusVariant>())
            return DBUS_TYPE_VARIANT_AS_STRING;

        return DBUS_TYPE_INVALID_AS_STRING;
    }
}

/*!
    \enum QDBusType::VariantListMode
    Defines how the guessFromVariant() function will behave when the QVariant is of type
    QVariant::List.

    \todo Improve the algorithm
*/

/*!
    Guesses the DBus type from the given variant.
*/
QDBusType QDBusType::guessFromVariant(const QVariant& variant, VariantListMode mode)
{
    if (variant.type() == QVariant::List) {
        // investigate deeper
        QDBusType t;
        t.d = new QDBusTypePrivate;

        if (mode == ListIsArray) {
            t.d->code = DBUS_TYPE_ARRAY;

            const QVariantList list = variant.toList();
            if (!list.isEmpty()) {
                // check if all elements have the same type
                QVariant::Type type = list.first().type();
                foreach (const QVariant& v, list)
                    if (type != v.type()) {
                        // at least one is different
                        type = QVariant::Invalid;
                        break;
                    }

                if (type != QVariant::Invalid) {
                    // all are of the same type
                    t.d->subTypes << guessFromVariant(list.first());
                    return t;
                }
            }

            // internal information has been lost or there are many types
            QDBusType nested;
            nested.d = new QDBusTypePrivate;
            nested.d->code = DBUS_TYPE_VARIANT;
            t.d->subTypes << nested;
            return t;
        }
        else {
            // treat it as a struct
            t.d->code = DBUS_TYPE_STRUCT;

            // add the elements:
            const QVariantList list = variant.toList();
            foreach (const QVariant& v, list)
                t.d->subTypes << guessFromVariant(v, mode);

            return t;
        }
    }
    else if (variant.type() == QVariant::Map) {
        // investigate deeper
        QDBusType t, t2, t3;
        t2.d = new QDBusTypePrivate;
        t2.d->code = DBUS_TYPE_DICT_ENTRY;

        // the key
        t3.d = new QDBusTypePrivate;
        t3.d->code = DBUS_TYPE_STRING;
        t2.d->subTypes << t3;

        const QVariantMap map = variant.toMap();
        if (!map.isEmpty()) {
            // check if all elements have the same type
            QVariantMap::const_iterator it = map.constBegin(),
                                       end = map.constEnd();
            QVariant::Type type = it.value().type();
            for ( ; it != end; ++it)
                if (type != it.value().type()) {
                    // at least one is different
                    type = QVariant::Invalid;
                    break;
                }

            if (type != QVariant::Invalid)
                t2.d->subTypes << guessFromVariant(map.constBegin().value());
            else {
                // multiple types
                t3.d->code = DBUS_TYPE_VARIANT;
                t2.d->subTypes << t3;
            }
        }
        else {
            // information lost
            t3.d->code = DBUS_TYPE_VARIANT;
            t2.d->subTypes << t3;
        }

        t.d = new QDBusTypePrivate;
        t.d->code = DBUS_TYPE_ARRAY;
        t.d->subTypes << t2;
        return t;
    }
    else
        return QDBusType(variant.type());
}

/*!
   \class QDBusTypeList
   \brief A list of DBus types.

   Represents zero or more DBus types in sequence, such as those used in argument lists
   or in subtypes of structs and maps.
*/

/*!
   \fn QDBusTypeList::QDBusTypeList()

   Default constructor.
 */

/*!
   \fn QDBusTypeList::QDBusTypeList(const QDBusTypeList& other)

   Copy constructor.
   \param other         the list to copy
 */

/*!
   \fn QDBusTypeList::QDBusTypeList(const QList<QDBusType>& other)

   Copy constructor.
   \param other         the list to copy
 */

/*!
   Constructs a type list by parsing the signature given.
   \param signature     the signature to be parsed
 */
QDBusTypeList::QDBusTypeList(const char* signature)
{
    if (!signature || !*signature)
        return;                 // empty

    // validate it first
    if ( !dbus_signature_validate(signature, 0) )
        return;

    // split it into components
    DBusSignatureIter iter;
    dbus_signature_iter_init(&iter, signature);

    do {
        *this << QDBusType(&iter);
    } while (dbus_signature_iter_next(&iter));
}

/*!
    \internal
    Constructs a type list by parsing the elements on this iterator level.

    \param iter         the iterator containing the elements on this level
*/
QDBusTypeList::QDBusTypeList(DBusSignatureIter* iter)
{
    do {
        QDBusType item(iter);
        if (!item.isValid()) {
            clear();
            return;
        }

        *this << item;
    } while (dbus_signature_iter_next(iter));
}

/*!
    Returns true if this type list can represent the inner components of a map.
*/
bool QDBusTypeList::canBeMap() const
{
    return size() == 2 && at(0).isBasic();
}

/*!
    Reconstructs the type signature that this type list represents.
*/
QByteArray QDBusTypeList::dbusSignature() const
{
    QByteArray retval;
    foreach (QDBusType t, *this)
        retval += t.dbusSignature();
    return retval;
}

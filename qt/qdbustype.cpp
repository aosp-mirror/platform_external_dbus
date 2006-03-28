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

#include "qdbustype_p.h"
#include "qdbustypehelper_p.h"
#include <dbus/dbus.h>

#include <QtCore/qstringlist.h>

class QDBusTypePrivate: public QSharedData
{
public:
    int code;
    mutable int qvariantType;
    mutable QByteArray signature;
    QDBusTypeList subTypes;

    inline QDBusTypePrivate()
        : code(0), qvariantType(QVariant::Invalid)
    { }
};

/*!
    \class QDBusType
    \brief Represents one single D-Bus type.
    \internal

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
    Constructs an empty (invalid) type.
*/
QDBusType::QDBusType()
    : d(0)
{
}

/*!
    Constructs the type based on the D-Bus type given by \a type.
*/
QDBusType::QDBusType(int type)
{
    char c[2] = { type, 0 };
    *this = QDBusType(c);
}

/*!
    Constructs the type based on the QVariant type given by \a type.

    \sa QVariant::Type
*/
QDBusType::QDBusType(QVariant::Type type)
{
    const char *sig = dbusSignature(type);

    // it never returns NULL
    // but it may return an empty string:
    if (sig[0] == '\0')
        return;

    if (qstrlen(sig) > 2) {
        *this = QDBusType(sig);
    } else {
        d = new QDBusTypePrivate;
        d->qvariantType = type;
        d->code = sig[0];
        if (sig[1] == '\0')
            // single-letter type
            return;
        else {
            // two-letter type
            // must be an array
            d->code = sig[0];
            QDBusType t;
            t.d = new QDBusTypePrivate;
            t.d->code = sig[1];
            d->subTypes << t;
        }
    }
}

/*!
    Parses the D-Bus signature given by \a signature and constructs the type it represents.
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
    \overload
    Parses the D-Bus signature given by \a str and constructs the type it represents.
*/
QDBusType::QDBusType(const QString& str)
{
    *this = QDBusType( str.toUtf8().constData() );
}

/*!
    \overload 
    Parses the D-Bus signature given by \a str and constructs the type it represents.
*/
QDBusType::QDBusType(const QByteArray& str)
{
    *this = QDBusType( str.constData() );
}

/*!
    \internal
    Creates a QDBusType object based on the current element pointed to by \a iter.
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
    Copies the type from the object \a other.
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
    Copies the type from the object given by \a other.
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
int QDBusType::qvariantType() const
{
    if (d && d->qvariantType != QVariant::Invalid)
        return d->qvariantType;

    if (!d)
        return QVariant::Invalid;

    return d->qvariantType = qvariantType(dbusSignature().constData());
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
*/
bool QDBusType::isBasic() const
{
    return d && dbus_type_is_basic(d->code);
}

/*!
    Returns true if this type is a container.
*/
bool QDBusType::isContainer() const
{
    return d && dbus_type_is_container(d->code);
}

/*!
    Returns the subtypes of this type, if this is a container.

    \sa isContainer()
*/
QDBusTypeList QDBusType::subTypes() const
{
    if (d)
        return d->subTypes;
    return QDBusTypeList();
}

/*!
    Returns true if this type is an array.

    \sa isContainer(), arrayElement()
*/
bool QDBusType::isArray() const
{
    return dbusType() == DBUS_TYPE_ARRAY;
}

/*!
    This is a convenience function that returns the element type of an array.
    If this object is not an array, it returns an invalid QDBusType.

    \sa isArray()
*/
QDBusType QDBusType::arrayElement() const
{
    if (isArray() && d->subTypes.count() == 1)
        return d->subTypes.first();
    return QDBusType();
}

/*!
    Returns true if this type is a map (i.e., an array of dictionary entries).

    \sa isContainer(), isArray(), arrayElement()
*/
bool QDBusType::isMap() const
{
    return arrayElement().dbusType() == DBUS_TYPE_DICT_ENTRY;
}

/*!
    If this object is a map, returns the (basic) type that corresponds to the key type.
    If this object is not a map, returns an invalid QDBusType.

    \sa isMap()
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

    \sa isMap()
*/
QDBusType QDBusType::mapValue() const
{
    if (isMap())
        return arrayElement().d->subTypes.at(1);
    return QDBusType();
}

/*!
    Returns true if this type is the same one as \a other.
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
    \fn QDBusType::operator!=(const QDBusType &other) const
    Returns true if the this type and the one given by \a other are different.
*/

/*!
    Converts the DBus type code \a type to QVariant::Type.
*/
int QDBusType::qvariantType(int type)
{
    char c[2] = { type, 0 };
    return qvariantType(c);
}

/*!
    Converts the DBus type signature \a signature to QVariant::Type.
*/
int QDBusType::qvariantType(const char* signature)
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

    case DBUS_TYPE_BYTE:
        return QMetaType::UChar;

    case DBUS_TYPE_INT16:
        return QMetaType::Short;

    case DBUS_TYPE_UINT16:
        return QMetaType::UShort;
        
    case DBUS_TYPE_INT32:
        return QVariant::Int;
        
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
        return QDBusTypeHelper<QVariant>::id();

    case DBUS_TYPE_ARRAY:       // special case
        switch (signature[1]) {
        case DBUS_TYPE_BOOLEAN:
            return QDBusTypeHelper<bool>::listId();

        case DBUS_TYPE_BYTE:
            return QVariant::ByteArray;

        case DBUS_TYPE_INT16:
            return QDBusTypeHelper<short>::listId();

        case DBUS_TYPE_UINT16:
            return QDBusTypeHelper<ushort>::listId();

        case DBUS_TYPE_INT32:
            return QDBusTypeHelper<int>::listId();

        case DBUS_TYPE_UINT32:
            return QDBusTypeHelper<uint>::listId();

        case DBUS_TYPE_INT64:
            return QDBusTypeHelper<qlonglong>::listId();

        case DBUS_TYPE_UINT64:
            return QDBusTypeHelper<qulonglong>::listId();

        case DBUS_TYPE_DOUBLE:
            return QDBusTypeHelper<double>::listId();

        case DBUS_TYPE_STRING:
        case DBUS_TYPE_OBJECT_PATH:
        case DBUS_TYPE_SIGNATURE:
            return QVariant::StringList;

        case DBUS_TYPE_VARIANT:
            return QVariant::List;

        case DBUS_DICT_ENTRY_BEGIN_CHAR:
            return QVariant::Map;

        default:
            return QVariant::List;
        }
    default:
        return QVariant::Invalid;

    }
}

/*!
    Converts the QVariant::Type \a t to a DBus type code.
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
        return DBUS_TYPE_INVALID; // invalid

    default:
        break;                  // avoid compiler warnings
    }

    if (int(t) == QDBusTypeHelper<QVariant>::id())
        return DBUS_TYPE_VARIANT;

    return DBUS_TYPE_INVALID;
}

/*!
    Converts the QVariant::Type \a t to a DBus type signature.
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
        if (int(t) == QDBusTypeHelper<QVariant>::id())
            return DBUS_TYPE_VARIANT_AS_STRING;
        if (int(t) == QDBusTypeHelper<bool>::listId())
            return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_BOOLEAN_AS_STRING;
        if (int(t) == QDBusTypeHelper<short>::listId())
            return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_INT16_AS_STRING;
        if (int(t) == QDBusTypeHelper<ushort>::listId())
            return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_UINT16_AS_STRING;
        if (int(t) == QDBusTypeHelper<int>::listId())
            return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_INT32_AS_STRING;
        if (int(t) == QDBusTypeHelper<uint>::listId())
            return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_UINT32_AS_STRING;
        if (int(t) == QDBusTypeHelper<qlonglong>::listId())
            return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_INT64_AS_STRING;
        if (int(t) == QDBusTypeHelper<qulonglong>::listId())
            return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_UINT64_AS_STRING;
        if (int(t) == QDBusTypeHelper<double>::listId())
            return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_DOUBLE_AS_STRING;

        return DBUS_TYPE_INVALID_AS_STRING;
    }
}

/*!
    \enum QDBusType::VariantListMode
    Defines how the guessFromVariant() function will behave when the QVariant is of type
    QVariant::List.
*/

/*!
    Guesses the DBus type from the given \a variant.
*/
QDBusType QDBusType::guessFromVariant(const QVariant& variant, VariantListMode mode)
{
    if (variant.type() == QVariant::List) {
        // investigate deeper
        QDBusType t;
        t.d = new QDBusTypePrivate;
        const QVariantList list = variant.toList();

        t.d->code = DBUS_TYPE_ARRAY;
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
        } else {
            // an array of "something"
            t.d->subTypes << QDBusType('v');
            return t;
        }
            
        // treat it as a struct
        t.d->code = DBUS_TYPE_STRUCT;
        foreach (const QVariant& v, list)
            t.d->subTypes << guessFromVariant(v, mode);
        
        return t;
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
        return QDBusType( QVariant::Type( variant.userType() ) );
}

/*!
   \class QDBusTypeList
   \brief A list of DBus types.
   \internal

   Represents zero or more DBus types in sequence, such as those used in argument lists
   or in subtypes of structs and maps.
*/

/*!
   \fn QDBusTypeList::QDBusTypeList()

   Default constructor.
 */

/*!
   \fn QDBusTypeList::QDBusTypeList(const QDBusTypeList& other)

   Copy constructor: copies the type list from \a other.
*/

/*!
   \fn QDBusTypeList::QDBusTypeList(const QList<QDBusType>& other)

   Copy constructor: copies the type list from \a other.
*/

/*!
   Constructs a type list by parsing the given \a signature.
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

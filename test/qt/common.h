#include <math.h>               // isnan

Q_DECLARE_METATYPE(QVariant)
Q_DECLARE_METATYPE(QList<bool>)
Q_DECLARE_METATYPE(QList<short>)
Q_DECLARE_METATYPE(QList<ushort>)
Q_DECLARE_METATYPE(QList<int>)
Q_DECLARE_METATYPE(QList<uint>)
Q_DECLARE_METATYPE(QList<qlonglong>)
Q_DECLARE_METATYPE(QList<qulonglong>)
Q_DECLARE_METATYPE(QList<double>)
#if 0
#include "../qdbusintrospection_p.h"

// just to make it easier:
typedef QDBusIntrospection::Interfaces InterfaceMap;
typedef QDBusIntrospection::Objects ObjectMap;
typedef QDBusIntrospection::Arguments ArgumentList;
typedef QDBusIntrospection::Annotations AnnotationsMap;
typedef QDBusIntrospection::Methods MethodMap;
typedef QDBusIntrospection::Signals SignalMap;
typedef QDBusIntrospection::Properties PropertyMap;

Q_DECLARE_METATYPE(QDBusIntrospection::Method)
Q_DECLARE_METATYPE(QDBusIntrospection::Signal)
Q_DECLARE_METATYPE(QDBusIntrospection::Property)
Q_DECLARE_METATYPE(MethodMap)
Q_DECLARE_METATYPE(SignalMap)
Q_DECLARE_METATYPE(PropertyMap)

inline QDBusIntrospection::Argument arg(const char* type, const char *name = 0)
{
    QDBusIntrospection::Argument retval;
    retval.type = QLatin1String(type);
    retval.name = QLatin1String(name);
    return retval;
}

template<typename T>
inline QMap<QString, T>& operator<<(QMap<QString, T>& map, const T& m)
{ map.insertMulti(m.name, m); return map; }

inline const char* mapName(const MethodMap&)
{ return "MethodMap"; }

inline const char* mapName(const SignalMap&)
{ return "SignalMap"; }

inline const char* mapName(const PropertyMap&)
{ return "PropertyMap"; }

QString printable(const QDBusIntrospection::Method& m)
{
    QString result = "method " + m.name + "(";
    foreach (QDBusIntrospection::Argument arg, m.inputArgs)
        result += QString("in %1 %2, ")
        .arg(arg.type, arg.name);
    foreach (QDBusIntrospection::Argument arg, m.outputArgs)
        result += QString("out %1 %2, ")
        .arg(arg.type, arg.name);
    AnnotationsMap::const_iterator it = m.annotations.begin();
    for ( ; it != m.annotations.end(); ++it)
        result += QString("%1 \"%2\", ").arg(it.key()).arg(it.value());

    result += ")";
    return result;
}    

QString printable(const QDBusIntrospection::Signal& s)
{
    QString result = "signal " + s.name + "(";
    foreach (QDBusIntrospection::Argument arg, s.outputArgs)
        result += QString("out %1 %2, ")
        .arg(arg.type, arg.name);
    AnnotationsMap::const_iterator it = s.annotations.begin();
    for ( ; it != s.annotations.end(); ++it)
        result += QString("%1 \"%2\", ").arg(it.key()).arg(it.value());

    result += ")";
    return result;
}    

QString printable(const QDBusIntrospection::Property& p)
{
    QString result;
    if (p.access == QDBusIntrospection::Property::Read)
        result = "property read %1 %2, ";
    else if (p.access == QDBusIntrospection::Property::Write)
        result = "property write %1 %2, ";
    else
        result = "property readwrite %1 %2, ";
    result = result.arg(p.type, p.name);
    
    AnnotationsMap::const_iterator it = p.annotations.begin();
    for ( ; it != p.annotations.end(); ++it)
        result += QString("%1 \"%2\", ").arg(it.key()).arg(it.value());

    return result;
}    

template<typename T>
char* printableMap(const QMap<QString, T>& map)
{
    QString contents = "\n";
    typename QMap<QString, T>::const_iterator it = map.begin();
    for ( ; it != map.end(); ++it) {
        if (it.key() != it.value().name)
            contents += it.value().name + ":";
        contents += printable(it.value());
        contents += ";\n";
    }

    QString result("%1(size = %2): {%3}");
    return qstrdup(qPrintable(result
                              .arg(mapName(map))
                              .arg(map.size())
                              .arg(contents)));
}

namespace QTest {
    template<>
    inline char* toString(const MethodMap& map)
    {
        return printableMap(map);
    }

    template<>
    inline char* toString(const SignalMap& map)
    {
        return printableMap(map);
    }

    template<>
    inline char* toString(const PropertyMap& map)
    {
        return printableMap(map);
    }
}
#endif
bool compare(const QVariantList &l1, const QVariantList &l2);
bool compare(const QVariantMap &m1, const QVariantMap &m2);
bool compare(const QVariant &v1, const QVariant &v2);

bool compare(const QList<double> &l1, const QList<double> &l2)
{
    if (l1.count() != l2.count())
        return false;

    QList<double>::ConstIterator it1 = l1.constBegin();
    QList<double>::ConstIterator it2 = l2.constBegin();
    QList<double>::ConstIterator end = l1.constEnd();
    for ( ; it1 != end; ++it1, ++it2)
        if (isnan(*it1) && isnan(*it2))
            continue;
        else if (*it1 != *it2)
            return false;
    return true;
}

bool compare(const QVariant &v1, const QVariant &v2)
{
    if (v1.userType() != v2.userType())
        return false;

    int id = v1.userType();
    if (id == QVariant::List)
        return compare(v1.toList(), v2.toList());

    else if (id == QVariant::Map)
        return compare(v1.toMap(), v2.toMap());

    else if (id < int(QVariant::UserType)) // yes, v1.type()
        // QVariant can compare
        return v1 == v2;

    else if (id == QMetaType::UChar)
        return qvariant_cast<uchar>(v1) == qvariant_cast<uchar>(v2);

    else if (id == QMetaType::Short)
        return qvariant_cast<short>(v1) == qvariant_cast<short>(v2);

    else if (id == QMetaType::UShort)
        return qvariant_cast<ushort>(v1) == qvariant_cast<ushort>(v2);
    
    else if (id == qMetaTypeId<QVariant>())
        return compare(qvariant_cast<QVariant>(v1), qvariant_cast<QVariant>(v2));

    else if (id == qMetaTypeId<QList<bool> >()) 
        return qvariant_cast<QList<bool> >(v1) == qvariant_cast<QList<bool> >(v2);

    else if (id == qMetaTypeId<QList<short> >())
        return qvariant_cast<QList<short> >(v1) == qvariant_cast<QList<short> >(v2);

    else if (id == qMetaTypeId<QList<ushort> >())
        return qvariant_cast<QList<ushort> >(v1) == qvariant_cast<QList<ushort> >(v2);

    else if (id == qMetaTypeId<QList<int> >())
        return qvariant_cast<QList<int> >(v1) == qvariant_cast<QList<int> >(v2);

    else if (id == qMetaTypeId<QList<uint> >())
        return qvariant_cast<QList<uint> >(v1) == qvariant_cast<QList<uint> >(v2);

    else if (id == qMetaTypeId<QList<qlonglong> >())
        return qvariant_cast<QList<qlonglong> >(v1) == qvariant_cast<QList<qlonglong> >(v2);

    else if (id == qMetaTypeId<QList<qulonglong> >())
        return qvariant_cast<QList<qulonglong> >(v2) == qvariant_cast<QList<qulonglong> >(v2);

    else if (id == qMetaTypeId<QList<double> >())
        return compare(qvariant_cast<QList<double> >(v1), qvariant_cast<QList<double> >(v2));

    else
        return false;           // unknown type
}

bool compare(const QVariantList &l1, const QVariantList &l2)
{
    if (l1.count() != l2.size())
        return false;
    QVariantList::ConstIterator i1 = l1.constBegin();
    QVariantList::ConstIterator i2 = l2.constBegin();
    QVariantList::ConstIterator end = l1.constEnd();
    for ( ; i1 != end; ++i1, ++i2) {
        if (!compare(*i1, *i2))
            return false;
    }
    return true;
}

bool compare(const QVariantMap &m1, const QVariantMap &m2)
{
    if (m1.count() != m2.size())
        return false;
    QVariantMap::ConstIterator i1 = m1.constBegin();
    QVariantMap::ConstIterator end = m1.constEnd();
    for ( ; i1 != end; ++i1) {
        QVariantMap::ConstIterator i2 = m2.find(i1.key());
        if (i2 == m2.constEnd())
            return false;
        if (!compare(*i1, *i2))
            return false;
    }
    return true;
}

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
    retval.type = QDBusType(type);
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
        .arg(arg.type.toString(QDBusType::ConventionalNames))
        .arg(arg.name);
    foreach (QDBusIntrospection::Argument arg, m.outputArgs)
        result += QString("out %1 %2, ")
        .arg(arg.type.toString(QDBusType::ConventionalNames))
        .arg(arg.name);
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
        .arg(arg.type.toString(QDBusType::ConventionalNames))
        .arg(arg.name);
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
    result = result.arg(p.type.toString(QDBusType::ConventionalNames)).arg(p.name);
    
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

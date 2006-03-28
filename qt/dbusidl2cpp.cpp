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

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qdatetime.h>
#include <QtCore/qfile.h>
#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qtextstream.h>
#include <QtCore/qset.h>

#include <dbus/qdbus.h>
#include "qdbusmetaobject_p.h"
#include "qdbusintrospection_p.h"

#define PROGRAMNAME     "dbusidl2cpp"
#define PROGRAMVERSION  "0.3"
#define PROGRAMCOPYRIGHT "Copyright (C) 2006 Trolltech AS. All rights reserved."

#define ANNOTATION_NO_WAIT      "org.freedesktop.DBus.Method.NoReply"

static const char cmdlineOptions[] = "a:c:hmNp:vV";
static const char *globalClassName;
static const char *proxyFile;
static const char *adaptorFile;
static const char *inputFile;
static bool skipNamespaces;
static bool verbose;
static bool includeMocs;
static QStringList wantedInterfaces;

static const char help[] =
    "Usage: " PROGRAMNAME " [options...] [idl-or-xml-file] [interfaces...]\n"
    "Produces the C++ code to implement the interfaces defined in the input file.\n"
    "If no options are given, the code is written to the standard output.\n"
    "\n"
    "Options:\n"
    "  -a <filename>    Write the adaptor code to <filename>\n"
    "  -c <classname>   Use <classname> as the class name for the generated classes\n"
    "  -h               Show this information\n"
    "  -m               Generate #include \"filename.moc\" statements in the .cpp files\n"
    "  -N               Don't use namespaces\n"
    "  -p <filename>    Write the proxy code to <filename>\n"
    "  -v               Be verbose.\n"
    "  -V               Show the program version and quit.\n"
    "\n"
    "If the file name given to the options -a and -p does not end in .cpp or .h, the\n"
    "program will automatically append the suffixes and produce both files.\n";

static const char includeList[] =
    "#include <QtCore/QByteArray>\n"
    "#include <QtCore/QList>\n"
    "#include <QtCore/QMap>\n"
    "#include <QtCore/QString>\n"
    "#include <QtCore/QStringList>\n"
    "#include <QtCore/QVariant>\n";

static const char forwardDeclarations[] =
    "class QByteArray;\n"
    "template<class T> class QList;\n"
    "template<class Key, class Value> class QMap;\n"
    "class QString;\n"
    "class QStringList;\n"
    "class QVariant;\n";

static void showHelp()
{
    printf("%s", help);
    exit(0);
}

static void showVersion()
{
    printf("%s version %s\n", PROGRAMNAME, PROGRAMVERSION);
    printf("D-Bus binding tool for Qt\n");
    exit(0);
}

static void parseCmdLine(int argc, char **argv)
{
    int c;
    opterr = true;
    while ((c = getopt(argc, argv, cmdlineOptions)) != -1)
        switch (c)
        {
        case 'a':
            adaptorFile = optarg;
            break;

        case 'c':
            globalClassName = optarg;
            break;
            
        case 'v':
            verbose = true;
            break;

        case 'm':
            includeMocs = true;
            break;

        case 'N':
            skipNamespaces = true;
            break;
            
        case 'h':
            showHelp();
            break;

        case 'V':
            showVersion();
            break;

        case 'p':
            proxyFile = optarg;
            break;

        case '?':
            exit(1);
        default:
            abort();
        }

    if (optind != argc)
        inputFile = argv[optind++];

    while (optind != argc)
        wantedInterfaces << QString::fromLocal8Bit(argv[optind++]);
}

static QDBusIntrospection::Interfaces readInput()
{
    QFile input(QFile::decodeName(inputFile));
    if (inputFile && QLatin1String("-") != inputFile) 
        input.open(QIODevice::ReadOnly);
    else
        input.open(stdin, QIODevice::ReadOnly);

    QByteArray data = input.readAll();

    // check if the input is already XML
    data = data.trimmed();
    if (data.startsWith("<!DOCTYPE ") || data.startsWith("<?xml") ||
        data.startsWith("<node") || data.startsWith("<interface"))
        // already XML
        return QDBusIntrospection::parseInterfaces(QString::fromUtf8(data));

    fprintf(stderr, "Cannot process input. Stop.\n");
    exit(1);
}

static void cleanInterfaces(QDBusIntrospection::Interfaces &interfaces)
{
    if (!wantedInterfaces.isEmpty()) {
        QDBusIntrospection::Interfaces::Iterator it = interfaces.begin();
        while (it != interfaces.end())
            if (!wantedInterfaces.contains(it.key()))
                it = interfaces.erase(it);
            else
                ++it;
    }
}        

// produce a header name from the file name
static QString header(const char *name)
{
    if (!name || (name[0] == '-' && name[1] == '\0'))
        return QString();

    QString retval = QFile::decodeName(name);
    if (!retval.endsWith(".h") && !retval.endsWith(".cpp") && !retval.endsWith(".cc"))
        retval.append(".h");

    return retval;
}

// produce a cpp name from the file name
static QString cpp(const char *name)
{
    if (!name || (name[0] == '-' && name[1] == '\0'))
        return QString();

    QString retval = QFile::decodeName(name);
    if (!retval.endsWith(".h") && !retval.endsWith(".cpp") && !retval.endsWith(".cc"))
        retval.append(".cpp");

    return retval;
}

static QTextStream &writeHeader(QTextStream &ts, bool changesWillBeLost)
{
    ts << "/*" << endl
       << " * This file was generated by " PROGRAMNAME " version " PROGRAMVERSION << endl
       << " * when processing input file " << (inputFile ? inputFile : "<stdin>") << endl
       << " *" << endl
       << " * " PROGRAMNAME " is " PROGRAMCOPYRIGHT << endl
       << " *" << endl
       << " * This is an auto-generated file." << endl;

    if (changesWillBeLost)
        ts << " * Do not edit! All changes made to it will be lost." << endl;

    ts << " */" << endl
       << endl;

    return ts;
}

enum ClassType { Proxy, Adaptor };
static QString classNameForInterface(const QString &interface, ClassType classType)
{
    if (globalClassName)
        return QLatin1String(globalClassName);

    QStringList parts = interface.split('.');

    QString retval;
    if (classType == Proxy)
        foreach (QString part, parts) {
            part[0] = part[0].toUpper();
            retval += part;
        }
    else {
        retval = parts.last();
        retval[0] = retval[0].toUpper();
    }

    if (classType == Proxy)
        retval += "Interface";
    else
        retval += "Adaptor";

    return retval;
}

static QByteArray qtTypeName(const QString &signature)
{
    QVariant::Type type = QDBusUtil::signatureToType(signature);
    if (type == QVariant::Invalid)
        qFatal("Got unknown type `%s'", qPrintable(signature));
    
    return QVariant::typeToName(type);
}

static QString nonConstRefArg(const QByteArray &arg)
{
    return QLatin1String(arg + " &");
}

static QString templateArg(const QByteArray &arg)
{
    if (!arg.endsWith('>'))
        return QLatin1String(arg);

    return QLatin1String(arg + ' ');
}

static QString constRefArg(const QByteArray &arg)
{
    if (!arg.startsWith('Q'))
        return QLatin1String(arg + ' ');
    else
        return QString("const %1 &").arg( QLatin1String(arg) );
}

static QStringList makeArgNames(const QDBusIntrospection::Arguments &inputArgs,
                                const QDBusIntrospection::Arguments &outputArgs =
                                QDBusIntrospection::Arguments())
{
    QStringList retval;
    for (int i = 0; i < inputArgs.count(); ++i) {
        const QDBusIntrospection::Argument &arg = inputArgs.at(i);
        QString name = arg.name;
        if (name.isEmpty())
            name = QString("in%1").arg(i);
        while (retval.contains(name))
            name += "_";
        retval << name;
    }
    for (int i = 0; i < outputArgs.count(); ++i) {
        const QDBusIntrospection::Argument &arg = outputArgs.at(i);
        QString name = arg.name;
        if (name.isEmpty())
            name = QString("out%1").arg(i);
        while (retval.contains(name))
            name += "_";
        retval << name;
    }
    return retval;
}

static void writeArgList(QTextStream &ts, const QStringList &argNames,
                         const QDBusIntrospection::Arguments &inputArgs,
                         const QDBusIntrospection::Arguments &outputArgs = QDBusIntrospection::Arguments())
{
    // input args:
    bool first = true;
    int argPos = 0;
    for (int i = 0; i < inputArgs.count(); ++i) {
        const QDBusIntrospection::Argument &arg = inputArgs.at(i);
        QString type = constRefArg(qtTypeName(arg.type));
        
        if (!first)
            ts << ", ";
        ts << type << argNames.at(argPos++);
        first = false;
    }

    argPos++;
    
    // output args
    // yes, starting from 1
    for (int i = 1; i < outputArgs.count(); ++i) {
        const QDBusIntrospection::Argument &arg = outputArgs.at(i);
        QString name = arg.name;

        if (!first)
            ts << ", ";
        ts << nonConstRefArg(qtTypeName(arg.type)) << argNames.at(argPos++);
        first = false;
    }
}

static QString stringify(const QString &data)
{
    QString retval;
    int i;
    for (i = 0; i < data.length(); ++i) {
        retval += '\"';
        for ( ; i < data.length() && data[i] != QChar('\n'); ++i)
            if (data[i] == '\"')
                retval += "\\\"";
            else
                retval += data[i];
        retval += "\"\n";
    }
    return retval;
}

static void writeProxy(const char *proxyFile, const QDBusIntrospection::Interfaces &interfaces)
{
    // open the file
    QString headerName = header(proxyFile);
    QFile file(headerName);
    if (!headerName.isEmpty())
        file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    else
        file.open(stdout, QIODevice::WriteOnly | QIODevice::Text);
    QTextStream hs(&file);

    QString cppName = cpp(proxyFile);
    QByteArray cppData;
    QTextStream cs(&cppData);

    // write the header:
    writeHeader(hs, true);

    // include guards:
    QString includeGuard;
    if (!headerName.isEmpty()) {
        includeGuard = headerName.toUpper().replace(QChar('.'), QChar('_'));
        int pos = includeGuard.lastIndexOf('/');
        if (pos != -1)
            includeGuard = includeGuard.mid(pos + 1);
    } else {
        includeGuard = QString("QDBUSIDL2CPP_PROXY");
    }
    includeGuard = QString("%1_%2%3")
                   .arg(includeGuard)
                   .arg(getpid())
                   .arg(QDateTime::currentDateTime().toTime_t());
    hs << "#ifndef " << includeGuard << endl
       << "#define " << includeGuard << endl
       << endl;

    // include our stuff:
    hs << "#include <QtCore/QObject>" << endl
       << includeList
       << "#include <dbus/qdbus.h>" << endl
       << endl;

    if (cppName != headerName) {
        writeHeader(cs, false);        
        cs << "#include \"" << headerName << "\"" << endl
           << endl;
    }
    
    foreach (const QDBusIntrospection::Interface *interface, interfaces) {
        QString className = classNameForInterface(interface->name, Proxy);

        // comment:
        hs << "/*" << endl
           << " * Proxy class for interface " << interface->name << endl
           << " */" << endl;
        cs << "/*" << endl
           << " * Implementation of interface class " << className << endl
           << " */" << endl
           << endl;

        // class header:
        hs << "class " << className << ": public QDBusAbstractInterface" << endl
           << "{" << endl
           << "    Q_OBJECT" << endl;
        
        // properties:
        foreach (const QDBusIntrospection::Property &property, interface->properties) {
            QByteArray type = qtTypeName(property.type);
            QString templateType = templateArg(type);
            QString constRefType = constRefArg(type);
            QString getter = property.name;
            QString setter = "set" + property.name;
            getter[0] = getter[0].toLower();
            setter[3] = setter[3].toUpper();

            hs << "    Q_PROPERTY(" << type << " " << property.name;

            // getter:
            if (property.access != QDBusIntrospection::Property::Write)
                // it's readble
                hs << " READ" << getter;

            // setter
            if (property.access != QDBusIntrospection::Property::Read)
                // it's writeable
                hs << " WRITE" << setter;

            hs << ")" << endl;
        }

        // the interface name
        hs << "public:" << endl
           << "    static inline const char *staticInterfaceName()" << endl
           << "    { return \"" << interface->name << "\"; }" << endl
           << endl;
        
        // constructors/destructors:
        hs << "public:" << endl
           << "    explicit " << className << "(QDBusAbstractInterfacePrivate *p);" << endl
           << endl
           << "    ~" << className << "();" << endl
           << endl;
        cs << className << "::" << className << "(QDBusAbstractInterfacePrivate *p)" << endl
           << "    : QDBusAbstractInterface(p)" << endl
           << "{" << endl
           << "}" << endl
           << endl
           << className << "::~" << className << "()" << endl
           << "{" << endl
           << "}" << endl
           << endl;


        // methods:
        hs << "public slots: // METHODS" << endl;
        foreach (const QDBusIntrospection::Method &method, interface->methods) {
            bool isAsync = method.annotations.value(ANNOTATION_NO_WAIT) == "true";
            if (isAsync && !method.outputArgs.isEmpty()) {
                fprintf(stderr, "warning: method %s in interface %s is marked 'async' but has output arguments.\n",
                        qPrintable(method.name), qPrintable(interface->name));
                continue;
            }
            
            hs << "    inline ";

            if (method.annotations.value("org.freedesktop.DBus.Deprecated") == "true")
                hs << "Q_DECL_DEPRECATED ";

            if (isAsync)
                hs << "Q_ASYNC void ";
            else if (method.outputArgs.isEmpty())
                hs << "QDBusReply<void> ";
            else {
                hs << "QDBusReply<" << templateArg(qtTypeName(method.outputArgs.first().type)) << "> ";
            }

            hs << method.name << "(";

            QStringList argNames = makeArgNames(method.inputArgs, method.outputArgs);
            writeArgList(hs, argNames, method.inputArgs, method.outputArgs);

            hs << ")" << endl
               << "    {" << endl;

            if (method.outputArgs.count() > 1)
                hs << "        QDBusMessage reply = call(QLatin1String(\"";
            else if (!isAsync)
                hs << "        return call(QLatin1String(\"";
            else
                hs << "        callAsync(QLatin1String(\"";

            // rebuild the method input signature:
            QString signature = QChar('.');
            foreach (const QDBusIntrospection::Argument &arg, method.inputArgs)
                signature += arg.type;
            if (signature.length() == 1)
                signature.clear();
            hs << method.name << signature << "\")";

            int argPos = 0;
            for (int i = 0; i < method.inputArgs.count(); ++i)
                hs << ", " << argNames.at(argPos++);

            // close the QDBusIntrospection::call/callAsync call
            hs << ");" << endl;

            argPos++;
            if (method.outputArgs.count() > 1) {
                hs << "        if (reply.type() == QDBusMessage::ReplyMessage) {" << endl;
                
                // yes, starting from 1
                for (int i = 1; i < method.outputArgs.count(); ++i)
                    hs << "            " << argNames.at(argPos++) << " = qvariant_cast<"
                       << templateArg(qtTypeName(method.outputArgs.at(i).type))
                       << ">(reply.at(" << i << "));" << endl;
                hs << "        }" << endl
                   << "        return reply;" << endl;
            }

            // close the function:
            hs << "    }" << endl
               << endl;
        }

        hs << "signals: // SIGNALS" << endl;
        foreach (const QDBusIntrospection::Signal &signal, interface->signals_) {
            hs << "    ";
            if (signal.annotations.value("org.freedesktop.DBus.Deprecated") == "true")
                hs << "Q_DECL_DEPRECATED ";
            
            hs << "void " << signal.name << "(";

            QStringList argNames = makeArgNames(signal.outputArgs);
            writeArgList(hs, argNames, signal.outputArgs);

            hs << ");" << endl; // finished for header
        }
        
        // close the class:
        hs << "};" << endl
           << endl;
    }

    if (!skipNamespaces) {
        QStringList last;
        QDBusIntrospection::Interfaces::ConstIterator it = interfaces.constBegin();
        do
        {
            QStringList current;
            QString name;
            if (it != interfaces.constEnd()) {
                current = it->constData()->name.split('.');
                name = current.takeLast();
            }
            
            int i = 0;
            while (i < current.count() && i < last.count() && current.at(i) == last.at(i))
                ++i;
        
            // i parts matched
            // close last.count() - i namespaces:
            for (int j = i; j < last.count(); ++j)
                hs << QString((last.count() - j - 1 + i) * 2, ' ') << "}" << endl;

            // open current.count() - i namespaces
            for (int j = i; j < current.count(); ++j)
                hs << QString(j * 2, ' ') << "namespace " << current.at(j) << " {" << endl;

            // add this class:
            if (!name.isEmpty()) {
                hs << QString(current.count() * 2, ' ')
                   << "typedef ::" << classNameForInterface(it->constData()->name, Proxy)
                   << " " << name << ";" << endl;
            }

            if (it == interfaces.constEnd())
                break;
            ++it;
            last = current;
        } while (true);
    }

    // close the include guard
    hs << "#endif" << endl;

    if (includeMocs)
        cs << endl
           << "#include \"" << proxyFile << ".moc\"" << endl;

    cs.flush();
    hs.flush();
    if (headerName == cppName)
        file.write(cppData);
    else {
        // write to cpp file
        QFile f(cppName);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
        f.write(cppData);
    }
}

static void writeAdaptor(const char *adaptorFile, const QDBusIntrospection::Interfaces &interfaces)
{
    // open the file
    QString headerName = header(adaptorFile);
    QFile file(headerName);
    if (!headerName.isEmpty())
        file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    else
        file.open(stdout, QIODevice::WriteOnly | QIODevice::Text);
    QTextStream hs(&file);
    
    QString cppName = cpp(adaptorFile);
    QByteArray cppData;
    QTextStream cs(&cppData);

    // write the headers
    writeHeader(hs, false);

    // include guards:
    QString includeGuard;
    if (!headerName.isEmpty()) {
        includeGuard = headerName.toUpper().replace(QChar('.'), QChar('_'));
        int pos = includeGuard.lastIndexOf('/');
        if (pos != -1)
            includeGuard = includeGuard.mid(pos + 1);
    } else {
        includeGuard = QString("QDBUSIDL2CPP_ADAPTOR");
    }
    includeGuard = QString("%1_%2%3")
                   .arg(includeGuard)
                   .arg(getpid())
                   .arg(QDateTime::currentDateTime().toTime_t());
    hs << "#ifndef " << includeGuard << endl
       << "#define " << includeGuard << endl
       << endl;

    // include our stuff:
    hs << "#include <QtCore/QObject>" << endl;
    if (cppName == headerName)
        hs << "#include <QtCore/QMetaObject>" << endl
           << "#include <QtCore/QVariant>" << endl;
    hs << "#include <dbus/qdbus.h>" << endl;
    
    if (cppName != headerName) {
        writeHeader(cs, false);        
        cs << "#include \"" << headerName << "\"" << endl
           << "#include <QtCore/QMetaObject>" << endl
           << includeList
           << endl;
        hs << forwardDeclarations;
    } else {
        hs << includeList;
    }

    hs << endl;

    foreach (const QDBusIntrospection::Interface *interface, interfaces) {
        QString className = classNameForInterface(interface->name, Adaptor);

        // comment:
        hs << "/*" << endl
           << " * Adaptor class for interface " << interface->name << endl
           << " */" << endl;
        cs << "/*" << endl
           << " * Implementation of adaptor class " << className << endl
           << " */" << endl
           << endl;

        // class header:
        hs << "class " << className << ": public QDBusAbstractAdaptor" << endl
           << "{" << endl
           << "    Q_OBJECT" << endl
           << "    Q_CLASSINFO(\"D-Bus Interface\", \"" << interface->name << "\")" << endl
           << "    Q_CLASSINFO(\"D-Bus Introspection\", \"\"" << endl
           << stringify(interface->introspection)
           << "        \"\")" << endl
           << "public:" << endl
           << "    " << className << "(QObject *parent);" << endl
           << "    virtual ~" << className << "();" << endl
           << endl;

        // constructor/destructor
        cs << className << "::" << className << "(QObject *parent)" << endl
           << "   : QDBusAbstractAdaptor(parent)" << endl
           << "{" << endl
           << "    // constructor" << endl
           << "    setAutoRelaySignals(true);" << endl
           << "}" << endl
           << endl
           << className << "::~" << className << "()" << endl
           << "{" << endl
           << "    // destructor" << endl
           << "}" << endl
           << endl;

        hs << "public: // PROPERTIES" << endl;
        foreach (const QDBusIntrospection::Property &property, interface->properties) {
            QByteArray type = qtTypeName(property.type);
            QString constRefType = constRefArg(type);
            QString getter = property.name;
            QString setter = "set" + property.name;
            getter[0] = getter[0].toLower();
            setter[3] = setter[3].toUpper();
            
            hs << "   Q_PROPERTY(" << type << " " << property.name;
            if (property.access != QDBusIntrospection::Property::Write)
                hs << " READ " << getter;
            if (property.access != QDBusIntrospection::Property::Read)
                hs << " WRITE " << setter;
            hs << ")" << endl;

            // getter:
            if (property.access != QDBusIntrospection::Property::Write) {
                hs << "    " << type << " " << getter << "() const;" << endl;
                cs << type << " "
                   << className << "::" << getter << "() const" << endl
                   << "{" << endl
                   << "    // get the value of property " << property.name << endl
                   << "    return qvariant_cast< " << type <<" >(object()->property(\"" << getter << "\"));" << endl
                   << "}" << endl
                   << endl;
            }

            // setter
            if (property.access != QDBusIntrospection::Property::Read) {
                hs << "    void " << setter << "(" << constRefType << "value);" << endl;
                cs << "void " << className << "::" << setter << "(" << constRefType << "value)" << endl
                   << "{" << endl
                   << "    // set the value of property " << property.name << endl
                   << "    object()->setProperty(\"" << getter << "\", value);" << endl
                   << "}" << endl
                   << endl;
            }

            hs << endl;
        }

        hs << "public slots: // METHODS" << endl;
        foreach (const QDBusIntrospection::Method &method, interface->methods) {
            bool isAsync = method.annotations.value(ANNOTATION_NO_WAIT) == "true";
            if (isAsync && !method.outputArgs.isEmpty()) {
                fprintf(stderr, "warning: method %s in interface %s is marked 'async' but has output arguments.\n",
                        qPrintable(method.name), qPrintable(interface->name));
                continue;
            }

            hs << "    ";
            if (method.annotations.value("org.freedesktop.DBus.Deprecated") == "true")
                hs << "Q_DECL_DEPRECATED ";

            QByteArray returnType;
            if (isAsync) {
                hs << "Q_ASYNC void ";
                cs << "void ";
            } else if (method.outputArgs.isEmpty()) {
                hs << "void ";
                cs << "void ";
            } else {
                returnType = qtTypeName(method.outputArgs.first().type);
                hs << returnType << " ";
                cs << returnType << " ";
            }

            QString name = method.name;
            hs << name << "(";
            cs << className << "::" << name << "(";

            QStringList argNames = makeArgNames(method.inputArgs, method.outputArgs);
            writeArgList(hs, argNames, method.inputArgs, method.outputArgs);
            writeArgList(cs, argNames, method.inputArgs, method.outputArgs);

            hs << ");" << endl; // finished for header
            cs << ")" << endl
               << "{" << endl
               << "    // handle method call " << interface->name << "." << method.name << endl;

            // create the return type
            int j = method.inputArgs.count();
            if (!returnType.isEmpty())
                cs << "    " << returnType << " " << argNames.at(j) << ";" << endl;

            // make the call
            if (method.inputArgs.count() <= 10 && method.outputArgs.count() <= 1) {
                // we can use QMetaObject::invokeMethod
                static const char invoke[] = "    QMetaObject::invokeMethod(object(), \"";
                cs << invoke << name << "\"";

                if (!method.outputArgs.isEmpty())
                    cs << ", Q_RETURN_ARG("
                       << qtTypeName(method.outputArgs.at(0).type)
                       << ", "
                       << argNames.at(method.inputArgs.count())
                       << ")";
                
                for (int i = 0; i < method.inputArgs.count(); ++i)
                    cs << ", Q_ARG("
                       << qtTypeName(method.inputArgs.at(i).type)
                       << ", "
                       << argNames.at(i)
                       << ")";
                    
                cs << ");" << endl;
            }

            cs << endl
               << "    // Alternative:" << endl
               << "    //";
            if (!method.outputArgs.isEmpty())
                cs << argNames.at(method.inputArgs.count()) << " = ";
            cs << "static_cast<YourObjectType *>(object())->" << name << "(";
            
            int argPos = 0;
            bool first = true;
            for (int i = 0; i < method.inputArgs.count(); ++i) {
                cs << (first ? "" : ", ") << argNames.at(argPos++);
                first = false;
            }
            ++argPos;           // skip retval, if any
            for (int i = 1; i < method.outputArgs.count(); ++i) {
                cs << (first ? "" : ", ") << argNames.at(argPos++);
                first = false;
            }

            cs << ");" << endl;
            if (!method.outputArgs.isEmpty())
                cs << "    return " << argNames.at(method.inputArgs.count()) << ";" << endl;
            cs << "}" << endl
               << endl;
        }

        hs << "signals: // SIGNALS" << endl;
        foreach (const QDBusIntrospection::Signal &signal, interface->signals_) {
            hs << "    ";
            if (signal.annotations.value("org.freedesktop.DBus.Deprecated") == "true")
                hs << "Q_DECL_DEPRECATED ";
            
            hs << "void " << signal.name << "(";

            QStringList argNames = makeArgNames(signal.outputArgs);
            writeArgList(hs, argNames, signal.outputArgs);

            hs << ");" << endl; // finished for header
        }

        // close the class:
        hs << "};" << endl
           << endl;        
    }

    // close the include guard
    hs << "#endif" << endl;

    if (includeMocs)
        cs << endl
           << "#include \"" << adaptorFile << ".moc\"" << endl;
    
    cs.flush();
    hs.flush();
    if (headerName == cppName)
        file.write(cppData);
    else {
        // write to cpp file
        QFile f(cppName);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
        f.write(cppData);
    }
}

int main(int argc, char **argv)
{
    parseCmdLine(argc, argv);

    QDBusIntrospection::Interfaces interfaces = readInput();
    cleanInterfaces(interfaces);

    writeProxy(proxyFile, interfaces);

    if (adaptorFile)
        writeAdaptor(adaptorFile, interfaces);

    return 0;
}
    
/*!
    \page dbusidl2cpp.html
    \title QtDBus IDL compiler (dbusidl2cpp)

    The QtDBus IDL compiler is a tool that can be used to parse interface descriptions and produce
    static code representing those interfaces, which can then be used to make calls to remote
    objects or implement said interfaces.

    \c dbusidl2dcpp has two modes of operation, that correspond to the two possible outputs it can
    produce: the interface (proxy) class or the adaptor class.The latter consists of both a C++
    header and a source file, which are meant to be edited and adapted to your needs.

    The \c dbusidl2dcpp tool is not meant to be run every time you compile your
    application. Instead, it's meant to be used when developing the code or when the interface
    changes.

    The adaptor classes generated by \c dbusidl2cpp are just a skeleton that must be completed. It
    generates, by default, calls to slots with the same name on the object the adaptor is attached
    to. However, you may modify those slots or the property accessor functions to suit your needs.
*/

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

#include <QByteArray>
#include <QString>
#include <QVarLengthArray>
#include <QFile>
#include <QProcess>
#include <QMetaObject>
#include <QList>
#include <QRegExp>

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "qdbusconnection.h"    // for the Export* flags
#include <dbus/dbus.h>          // for the XML DOCTYPE declaration

// in qdbusxmlgenerator.cpp
extern QDBUS_EXPORT QString qDBusGenerateMetaObjectXml(QString interface, const QMetaObject *mo,
                                                       const QMetaObject *base, int flags);

#define PROGRAMNAME     "dbuscpp2xml"
#define PROGRAMVERSION  "0.1"
#define PROGRAMCOPYRIGHT "Copyright (C) 2006 Trolltech AS. All rights reserved."

static const char cmdlineOptions[] = "psmaPSMAo:";
static const char *outputFile;
static int flags;

static const char help[] =
    "Usage: " PROGRAMNAME " [options...] [files...]\n"
    "Parses the C++ source or header file containing a QObject-derived class and\n"
    "produces the D-Bus Introspection XML."
    "\n"
    "Options:\n"
    "  -p|-s|-m       Only parse scriptable Properties, Signals and Methods (slots)\n"
    "  -P|-S|-M       Parse all Properties, Signals and Methods (slots)\n"
    "  -a             Output all scriptable contents (equivalent to -psm)\n"
    "  -A             Output all contents (equivalent to -PSM)\n"
    "  -o <filename>  Write the output to file <filename>\n"
    "  -h             Show this information\n"
    "  -V             Show the program version and quit.\n"
    "\n";

class MocParser
{
    void parseError();
    QByteArray readLine();
    void loadIntData(uint *&data);
    void loadStringData(char *&stringdata);

    QIODevice *input;
    const char *filename;
    int line;
public:
    ~MocParser();
    void parse(const char *filename, QIODevice *input, int lineNumber = 0);

    QList<QMetaObject> objects;
};
    
void MocParser::parseError()
{
    fprintf(stderr, PROGRAMNAME ": error parsing input file '%s' line %d \n", filename, line);
    exit(1);
}

QByteArray MocParser::readLine()
{
    ++line;
    return input->readLine();
}

void MocParser::loadIntData(uint *&data)
{
    data = 0;                   // initialise
    QVarLengthArray<uint> array;
    QRegExp rx("(\\d+|0x[0-9abcdef]+)", Qt::CaseInsensitive);

    while (!input->atEnd()) {
        QString line = QLatin1String(readLine());
        int pos = line.indexOf("//");
        if (pos != -1)
            line.truncate(pos); // drop comments

        if (line == "};\n") {
            // end of data
            data = new uint[array.count()];
            memcpy(data, array.data(), array.count() * sizeof(*data));
            return;
        }

        pos = 0;
        while ((pos = rx.indexIn(line, pos)) != -1) {
            QString num = rx.cap(1);
            if (num.startsWith("0x"))
                array.append(num.mid(2).toUInt(0, 16));
            else
                array.append(num.toUInt());
            pos += rx.matchedLength();
        }
    }

    parseError();
}

void MocParser::loadStringData(char *&stringdata)
{
    stringdata = 0;
    QVarLengthArray<char, 1024> array;

    while (!input->atEnd()) {
        QByteArray line = readLine();
        if (line == "};\n") {
            // end of data
            stringdata = new char[array.count()];
            memcpy(stringdata, array.data(), array.count() * sizeof(*stringdata));
            return;
        }

        int start = line.indexOf('"');
        if (start == -1)
            parseError();

        int len = line.length() - 1;
        line.truncate(len);     // drop ending \n
        if (line.at(len - 1) != '"')
            parseError();

        --len;
        ++start;
        for ( ; start < len; ++start)
            if (line.at(start) == '\\') {
                // parse escaped sequence
                ++start;
                if (start == len)
                    parseError();

                QChar c(QLatin1Char(line.at(start)));
                if (!c.isDigit()) {
                    switch (c.toLatin1()) {
                    case 'a':
                        array.append('\a');
                        break;
                    case 'b':
                        array.append('\b');
                        break;
                    case 'f':
                        array.append('\f');
                        break;
                    case 'n':
                        array.append('\n');
                        break;
                    case 'r':
                        array.append('\r');
                        break;
                    case 't':
                        array.append('\t');
                        break;
                    case 'v':
                        array.append('\v');
                        break;
                    case '\\':
                    case '?':
                    case '\'':
                    case '"':
                        array.append(c.toLatin1());
                        break;

                    case 'x':
                        if (start + 2 <= len)
                            parseError();
                        array.append(char(line.mid(start + 1, 2).toInt(0, 16)));
                        break;
                        
                    default:
                        array.append(c.toLatin1());
                        fprintf(stderr, PROGRAMNAME ": warning: invalid escape sequence '\\%c' found in input",
                                c.toLatin1());
                    }
                } else {
                    // octal
                    QRegExp octal("([0-7]+)");
                    if (octal.indexIn(QLatin1String(line), start) == -1)
                        parseError();
                    array.append(char(octal.cap(1).toInt(0, 8)));
                }
            } else {
                array.append(line.at(start));
            }
    }

    parseError();
}                    

void MocParser::parse(const char *fname, QIODevice *io, int lineNumber)
{
    filename = fname;
    input = io;
    line = lineNumber;

    while (!input->atEnd()) {
        QByteArray line = readLine();
        if (line.startsWith("static const uint qt_meta_data_")) {
            // start of new class data
            uint *data;
            loadIntData(data);

            // find the start of the string data
            do {
                line = readLine();
                if (input->atEnd())
                    parseError();
            } while (!line.startsWith("static const char qt_meta_stringdata_"));

            char *stringdata;
            loadStringData(stringdata);

            QMetaObject mo;
            mo.d.superdata = &QObject::staticMetaObject;
            mo.d.stringdata = stringdata;
            mo.d.data = data;
            mo.d.extradata = 0;
            objects.append(mo);
        }
    }

    fname = 0;
    input = 0;
}

MocParser::~MocParser()
{
    foreach (QMetaObject mo, objects) {
        delete const_cast<char *>(mo.d.stringdata);
        delete const_cast<uint *>(mo.d.data);
    }
}

static void showHelp()
{
    printf("%s", help);
    exit(0);
}

static void showVersion()
{
    printf("%s version %s\n", PROGRAMNAME, PROGRAMVERSION);
    printf("D-Bus QObject-to-XML converter\n");
    exit(0);
}

static void parseCmdLine(int argc, char **argv)
{
    int c;
    opterr = true;
    while ((c = getopt(argc, argv, cmdlineOptions)) != -1)
        switch (c)
        {
        case 'p':
            flags |= QDBusConnection::ExportProperties;
            break;

        case 's':
            flags |= QDBusConnection::ExportSignals;
            break;

        case 'm':
            flags |= QDBusConnection::ExportSlots;
            break;

        case 'a':
            flags |= QDBusConnection::ExportContents;
            break;

        case 'P':
            flags |= QDBusConnection::ExportAllProperties;
            break;

        case 'S':
            flags |= QDBusConnection::ExportAllSignals;
            break;

        case 'M':
            flags |= QDBusConnection::ExportAllSlots;
            break;

        case 'A':
            flags |= QDBusConnection::ExportAllContents;
            break;

        case 'o':
            outputFile = optarg;
            break;

        case 'h':
            showHelp();
            break;

        case 'V':
            showVersion();
            break;

        case '?':
            exit(1);
        default:
            abort();
        }

    if (flags == 0)
        flags = QDBusConnection::ExportAllContents;
}

int main(int argc, char **argv)
{
    MocParser parser;
    parseCmdLine(argc, argv);

    for (int i = 1; i < argc; ++i) {
        FILE *in = fopen(argv[i], "r");
        if (in == 0) {
            fprintf(stderr, PROGRAMNAME ": could not open '%s': %s\n",
                    argv[i], strerror(errno));
            return 1;
        }

        QFile f;
        f.open(in, QIODevice::ReadOnly);
        f.readLine();

        QByteArray line = f.readLine();
        if (line.contains("Meta object code from reading C++ file"))
            // this is a moc-generated file
            parser.parse(argv[i], &f, 3);
        else {
            // run moc on this file
            QProcess proc;
            proc.start("moc", QStringList() << QFile::encodeName(argv[i]));
            
            if (!proc.waitForStarted()) {
                fprintf(stderr, PROGRAMNAME ": could not execute moc! Aborting.\n");
                return 1;
            }

            proc.closeWriteChannel();

            if (!proc.waitForFinished() || proc.exitStatus() != QProcess::NormalExit ||
                proc.exitCode() != 0) {
                // output the moc errors:
                fprintf(stderr, "%s", proc.readAllStandardError().constData());
                fprintf(stderr, PROGRAMNAME ": exit code %d from moc. Aborting\n", proc.exitCode());
                return 1;
            }
            fprintf(stderr, "%s", proc.readAllStandardError().constData());

            parser.parse(argv[i], &proc, 1);
        }

        f.close();
        fclose(in);
    }

    FILE *output = stdout;
    if (outputFile != 0) {
        output = fopen(outputFile, "w");
        if (output == 0) {
            fprintf(stderr, PROGRAMNAME ": could not open output file '%s': %s",
                    outputFile, strerror(errno));
            return 1;
        }
    }

    fprintf(output, "%s<node>\n", DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);
    foreach (QMetaObject mo, parser.objects) {
        QString xml = qDBusGenerateMetaObjectXml(QString(), &mo, &QObject::staticMetaObject,
                                                 flags);
        fprintf(output, "%s", qPrintable(xml));
    }
    fprintf(output, "</node>\n");

    if (output != stdout)
        fclose(output);
}


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
#include <qcoreapplication.h>
#include <QtTest/QtTest>

#include <dbus/qdbus.h>

class tst_QDBusType: public QObject
{
    Q_OBJECT

private slots:
    void fromType_data();
    void fromType();
    void fromSignature_data();
    void fromSignature();
    void arrayOf_data();
    void arrayOf();
    void mapOf_data();
    void mapOf();
};

inline QTestData &operator<<(QTestData &data, QVariant::Type t)
{
    return data << int(t);
}

void tst_QDBusType::fromType_data()
{
    fromSignature_data();
}

void tst_QDBusType:: arrayOf_data()
{
    fromSignature_data();
}

void tst_QDBusType::mapOf_data()
{
    fromSignature_data();
}

void tst_QDBusType::fromSignature_data()
{
    QTest::addColumn<QString>("signature");
    QTest::addColumn<char>("type");
    QTest::addColumn<int>("qvariantType");
    QTest::addColumn<bool>("isValid");
    QTest::addColumn<bool>("isBasic");
    QTest::addColumn<bool>("isContainer");
    QTest::addColumn<int>("subtypeCount");

    QTest::newRow("null") << QString() << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("empty") << QString("") << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("invalid") << QString("~") << '\0' << QVariant::Invalid << false << false << false << 0;

    // integers:
    QTest::newRow("byte")    << "y" << 'y' << QVariant::UInt << true << true << false << 0;
    QTest::newRow("boolean") << "b" << 'b' << QVariant::Bool << true << true << false << 0;
    QTest::newRow("int16")   << "n" << 'n' << QVariant::Int << true << true << false << 0;
    QTest::newRow("uint16")  << "q" << 'q' << QVariant::UInt << true << true << false << 0;
    QTest::newRow("int32")   << "i" << 'i' << QVariant::Int << true << true << false << 0;
    QTest::newRow("uint32")  << "u" << 'u' << QVariant::UInt << true << true << false << 0;
    QTest::newRow("int64")   << "x" << 'x' << QVariant::LongLong << true << true << false << 0;
    QTest::newRow("uint64")  << "t" << 't' << QVariant::ULongLong << true << true << false << 0;

    // double:
    QTest::newRow("double")  << "d" << 'd' << QVariant::Double << true << true << false << 0;

    // string types:
    QTest::newRow("string")  << "s" << 's' << QVariant::String << true << true << false << 0;
    QTest::newRow("objpath") << "o" << 'o' << QVariant::String << true << true << false << 0;
    QTest::newRow("signature")<<"g" << 'g' << QVariant::String << true << true << false << 0;

    // variant
    QTest::newRow("variant") << "v" << 'v' << QVariant::UserType << true << false << true << 0;

    // compound types:
    QTest::newRow("struct-empty")       << "()" << '\0' << QVariant::Invalid << false << false << false  << 0;
    QTest::newRow("struct-invalid")     << "(~)" << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("struct-unterminated")<< "(iii" << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("struct-bad-nest")    << "(i(i)((i)i)" << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("struct1")            << "(i)" << 'r' << QVariant::List << true << false << true  << 1;
    QTest::newRow("struct2")            << "(ii)" << 'r' << QVariant::List << true << false << true  << 2;

    QTest::newRow("array-empty")        << "a" << '\0' << QVariant::Invalid << false << false << false  << 0;
    QTest::newRow("array-invalid")      << "a~" << '\0' << QVariant::Invalid << false << false << false  << 0;
    QTest::newRow("array-simple")       << "ab" << 'a' << QVariant::List << true << false << true  << 1;
    QTest::newRow("bytearray")          << "ay" << 'a' << QVariant::ByteArray << true << false << true << 1;
    QTest::newRow("stringlist")         << "as" << 'a' << QVariant::StringList << true << false << true << 1;
    
    QTest::newRow("map-empty")          << "e" << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("map-invalid1")       << "a{}" << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("map-invalid2")       << "a{~}" << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("map-invalid3")       << "a{e}" << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("map-invalid4")       << "a{i}" << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("map-invalid5")       << "a{(i)d}" << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("map-invalid6")       << "{}" << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("map-invalid7")       << "{i}" << '\0' << QVariant::Invalid << false << false << false << 0;
    //QTest::newRow("map-invalid8")       << "{is}" << '\0' << QVariant::Invalid << false << false << false << 0; // this is valid when "a" is prepended
    QTest::newRow("map-bad-nesting")    << "a{i(s}" << '\0' << QVariant::Invalid << false << false << false << 0;
    QTest::newRow("map-ok1")            << "a{is}" << 'a' << QVariant::Map << true << false << true << 1;
    QTest::newRow("map-ok2")            << "a{sv}" << 'a' << QVariant::Map << true << false << true << 1;

    // compound of compounds:
    QTest::newRow("struct-struct")      << "((i))" << 'r' << QVariant::List << true << false << true  << 1;
    QTest::newRow("struct-structs")     << "((ii)d(i))" << 'r' << QVariant::List << true << false << true  << 3;
    QTest::newRow("map-struct")         << "a{s(ii)}" << 'a' << QVariant::Map << true << false << true << 1;
    QTest::newRow("map-stringlist")     << "a{sas}" << 'a' << QVariant::Map << true << false << true << 1;
    QTest::newRow("map-map")            << "a{ia{sv}}" << 'a' << QVariant::Map << true << false << true << 1;
    QTest::newRow("array-struct")       << "a(ii)" << 'a' << QVariant::List << true << false << true << 1;
    QTest::newRow("array-array")        << "aai" << 'a' << QVariant::List << true << false << true << 1;
    QTest::newRow("array-map")          << "aa{sv}" << 'a' << QVariant::List << true << false << true << 1;
}

void tst_QDBusType::fromType()
{
    QFETCH(QString, signature);
    if (signature.length() != 1)
        // can't transform to typecode
        return;
    
    QFETCH(char, type);
    QFETCH(int, qvariantType);
    QFETCH(bool, isValid);
    QFETCH(bool, isBasic);
    QFETCH(bool, isContainer);

    QDBusType t(signature.at(0).toLatin1());

    QCOMPARE((char)t.dbusType(), type);
    QCOMPARE(t.qvariantType(), QVariant::Type(qvariantType));
    QCOMPARE(t.isValid(), isValid);
    QCOMPARE(t.isBasic(), isBasic);
    QCOMPARE(t.isContainer(), isContainer);
}

void tst_QDBusType::fromSignature()
{
    QFETCH(QString, signature);
    QFETCH(char, type);
    QFETCH(int, qvariantType);
    QFETCH(bool, isValid);
    QFETCH(bool, isBasic);
    QFETCH(bool, isContainer);
    QFETCH(int, subtypeCount);

    QDBusType t(signature);

    QCOMPARE((char)t.dbusType(), type);
    QCOMPARE(t.qvariantType(), QVariant::Type(qvariantType));
    QCOMPARE(t.isValid(), isValid);
    QCOMPARE(t.isBasic(), isBasic);
    QCOMPARE(t.isContainer(), isContainer);

    if (isValid)
        QCOMPARE(QLatin1String(t.dbusSignature()), signature);

    QCOMPARE(t.subTypes().count(), subtypeCount);
}

void tst_QDBusType::arrayOf()
{
    QFETCH(QString, signature);
    QFETCH(char, type);
    QFETCH(int, qvariantType);
    QFETCH(bool, isValid);
    QFETCH(bool, isBasic);
    QFETCH(bool, isContainer);
    QFETCH(int, subtypeCount);

    QDBusType arr("a" + signature.toLatin1());
    QCOMPARE(arr.isValid(), isValid);
    QVERIFY(!arr.isBasic());

    if (isValid) {
        QVERIFY(arr.isContainer());
        QVERIFY(arr.isArray());
        QCOMPARE((char)arr.dbusType(), 'a');
        QCOMPARE(arr.subTypes().count(), 1);

        // handle special cases:
        if (type == 'y')
            QCOMPARE(arr.qvariantType(), QVariant::ByteArray);
        else if (type == 's' || type == 'o' || type == 'g')
            QCOMPARE(arr.qvariantType(), QVariant::StringList);
        else
            QCOMPARE(arr.qvariantType(), QVariant::List);

        // handle the array element now:
        QDBusType t = arr.arrayElement();

        QCOMPARE((char)t.dbusType(), type);
        QCOMPARE(t.qvariantType(), QVariant::Type(qvariantType));
        QCOMPARE(t.isValid(), isValid);
        QCOMPARE(t.isBasic(), isBasic);
        QCOMPARE(t.isContainer(), isContainer);

        QCOMPARE(QLatin1String(t.dbusSignature()), signature);

        QCOMPARE(t.subTypes().count(), subtypeCount);
    }
}

void tst_QDBusType::mapOf()
{
    QFETCH(QString, signature);
    QFETCH(char, type);
    QFETCH(int, qvariantType);
    QFETCH(bool, isValid);
    QFETCH(bool, isBasic);
    QFETCH(bool, isContainer);
    QFETCH(int, subtypeCount);

    QDBusType map("a{s" + signature.toLatin1() + '}');
    QCOMPARE(map.isValid(), isValid);
    QVERIFY(!map.isBasic());

    if (isValid) {
        QVERIFY(map.isContainer());
        QVERIFY(map.isArray());
        QVERIFY(map.isMap());
        QCOMPARE((char)map.dbusType(), 'a');
        QCOMPARE(map.subTypes().count(), 1);

        // handle the array element now:
        QDBusType dict_entry = map.arrayElement();
        QVERIFY(dict_entry.isValid());
        QVERIFY(dict_entry.isContainer());
        QVERIFY(!dict_entry.isMap());
        QVERIFY(!dict_entry.isArray());

        QVERIFY(map.mapKey().isBasic());

        // handle the value:
        QDBusType t = map.mapValue();        

        QCOMPARE((char)t.dbusType(), type);
        QCOMPARE(t.qvariantType(), QVariant::Type(qvariantType));
        QCOMPARE(t.isValid(), isValid);
        QCOMPARE(t.isBasic(), isBasic);
        QCOMPARE(t.isContainer(), isContainer);

        QCOMPARE(QLatin1String(t.dbusSignature()), signature);

        QCOMPARE(t.subTypes().count(), subtypeCount);
    }
}    

QTEST_MAIN(tst_QDBusType)

#include "tst_qdbustype.moc"

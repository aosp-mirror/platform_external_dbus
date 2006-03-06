#include <qcoreapplication.h>
#include <qdebug.h>

#include <QtTest/QtTest>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/qdbus.h>

#include "common.h"

Q_DECLARE_METATYPE(QVariant)

const char *slotSpy;
QString propSpy;

namespace QTest {
    char *toString(QDBusMessage::MessageType t)
    {
        switch (t)
        {
        case QDBusMessage::InvalidMessage:
            return qstrdup("InvalidMessage");
        case QDBusMessage::MethodCallMessage:
            return qstrdup("MethodCallMessage");
        case QDBusMessage::ReplyMessage:
            return qstrdup("ReplyMessage");
        case QDBusMessage::ErrorMessage:
            return qstrdup("ErrorMessage");
        case QDBusMessage::SignalMessage:
            return qstrdup("SignalMessage");
        default:
            return 0;
        }
    }
}

class tst_QDBusAbstractAdaptor: public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void methodCalls_data();
    void methodCalls();
    void signalEmissions_data();
    void signalEmissions();
    void sameSignalDifferentPaths();
    void overloadedSignalEmission_data();
    void overloadedSignalEmission();
    void readProperties();
    void writeProperties();
    void adaptorIntrospection_data();
    void adaptorIntrospection();
    void objectTreeIntrospection();
};

class QDBusSignalSpy: public QObject
{
    Q_OBJECT

public slots:
    void slot(const QDBusMessage &msg)
    {
        ++count;
        interface = msg.interface();
        name = msg.name();
        signature = msg.signature();
        value.clear();
        if (msg.count())
            value = msg.at(0);
    }

public:
    QDBusSignalSpy() : count(0) { }

    int count;
    QString interface;
    QString name;
    QString signature;
    QVariant value;
};

class Interface1: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "local.Interface1");
public:
    Interface1(QObject *parent) : QDBusAbstractAdaptor(parent)
    { }

    static QDBusIntrospection::Methods methodData;
    static QDBusIntrospection::Signals signalData;
    static QDBusIntrospection::Properties propertyData;
};

class Interface2: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "local.Interface2");
    Q_PROPERTY(QString prop1 READ prop1);
    Q_PROPERTY(QString prop2 READ prop2 WRITE setProp2);
public:
    Interface2(QObject *parent) : QDBusAbstractAdaptor(parent)
    { setAutoRelaySignals(true); }

    QString prop1() const
    { return __PRETTY_FUNCTION__; }

    QString prop2() const
    { return __PRETTY_FUNCTION__; }

    void setProp2(const QString &value)
    { slotSpy = __PRETTY_FUNCTION__; propSpy = value; }

    void emitSignal(const QString &, const QVariant &)
    { emit signal(); }

public slots:
    void method() { slotSpy = __PRETTY_FUNCTION__; }

signals:
    void signal();

public:
    static QDBusIntrospection::Methods methodData;
    static QDBusIntrospection::Signals signalData;
    static QDBusIntrospection::Properties propertyData;    
};

class Interface3: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "local.Interface3");
    Q_PROPERTY(QString prop1 READ prop1);
    Q_PROPERTY(QString prop2 READ prop2 WRITE setProp2);
public:
    Interface3(QObject *parent) : QDBusAbstractAdaptor(parent)
    { setAutoRelaySignals(true); }

    QString prop1() const
    { return __PRETTY_FUNCTION__; }

    QString prop2() const
    { return __PRETTY_FUNCTION__; }

    void setProp2(const QString &value)
    { slotSpy = __PRETTY_FUNCTION__; propSpy = value; }

    void emitSignal(const QString &name, const QVariant &value)
    {
        if (name == "signalVoid")
            emit signalVoid();
        else if (name == "signalInt")
            emit signalInt(value.toInt());
        else if (name == "signalString")
            emit signalString(value.toString());
    }

public slots:
    void methodVoid() { slotSpy = __PRETTY_FUNCTION__; }
    void methodInt(int) { slotSpy = __PRETTY_FUNCTION__; }
    void methodString(QString) { slotSpy = __PRETTY_FUNCTION__; }

signals:
    void signalVoid();
    void signalInt(int);
    void signalString(const QString &);

public:
    static QDBusIntrospection::Methods methodData;
    static QDBusIntrospection::Signals signalData;
    static QDBusIntrospection::Properties propertyData;    
};

class Interface4: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "local.Interface4");
    Q_PROPERTY(QString prop1 READ prop1);
    Q_PROPERTY(QString prop2 READ prop2 WRITE setProp2);
public:
    Interface4(QObject *parent) : QDBusAbstractAdaptor(parent)
    { setAutoRelaySignals(true); }

    QString prop1() const
    { return __PRETTY_FUNCTION__; }

    QString prop2() const
    { return __PRETTY_FUNCTION__; }

    void setProp2(const QString &value)
    { slotSpy = __PRETTY_FUNCTION__; propSpy = value; }

    void emitSignal(const QString &, const QVariant &value)
    {
        switch (value.type())
        {
        case QVariant::Invalid:
            emit signal();
            break;
        case QVariant::Int:
            emit signal(value.toInt());
            break;
        case QVariant::String:
            emit signal(value.toString());
            break;
        default:
            break;
        }
    }

public slots:
    void method() { slotSpy = __PRETTY_FUNCTION__; }
    void method(int) { slotSpy = __PRETTY_FUNCTION__; }
    void method(QString) { slotSpy = __PRETTY_FUNCTION__; }

signals:
    void signal();
    void signal(int);
    void signal(const QString &);

public:
    static QDBusIntrospection::Methods methodData;
    static QDBusIntrospection::Signals signalData;
    static QDBusIntrospection::Properties propertyData;    
};


QDBusIntrospection::Methods Interface1::methodData;
QDBusIntrospection::Signals Interface1::signalData;
QDBusIntrospection::Properties Interface1::propertyData;    
QDBusIntrospection::Methods Interface2::methodData;
QDBusIntrospection::Signals Interface2::signalData;
QDBusIntrospection::Properties Interface2::propertyData;    
QDBusIntrospection::Methods Interface3::methodData;
QDBusIntrospection::Signals Interface3::signalData;
QDBusIntrospection::Properties Interface3::propertyData;    
QDBusIntrospection::Methods Interface4::methodData;
QDBusIntrospection::Signals Interface4::signalData;
QDBusIntrospection::Properties Interface4::propertyData;

void tst_QDBusAbstractAdaptor::initTestCase()
{
    QDBusIntrospection::Method method;
    method.name = "Method";
    Interface2::methodData << method;
    Interface4::methodData << method;
    method.inputArgs << arg("i");
    Interface4::methodData << method;
    method.inputArgs.clear();
    method.inputArgs << arg("s");
    Interface4::methodData << method;

    method.name = "MethodVoid";
    method.inputArgs.clear();
    Interface3::methodData << method;
    method.name = "MethodInt";
    method.inputArgs << arg("i");
    Interface3::methodData << method;
    method.name = "MethodString";
    method.inputArgs.clear();
    method.inputArgs << arg("s");
    Interface3::methodData << method;

    QDBusIntrospection::Signal signal;
    signal.name = "Signal";
    Interface2::signalData << signal;
    Interface4::signalData << signal;
    signal.outputArgs << arg("i");
    Interface4::signalData << signal;
    signal.outputArgs.clear();
    signal.outputArgs << arg("s");
    Interface4::signalData << signal;

    signal.name = "SignalVoid";
    signal.outputArgs.clear();
    Interface3::signalData << signal;
    signal.name = "SignalInt";
    signal.outputArgs << arg("i");
    Interface3::signalData << signal;
    signal.name = "SignalString";
    signal.outputArgs.clear();
    signal.outputArgs << arg("s");
    Interface3::signalData << signal;

    QDBusIntrospection::Property prop;
    prop.name = "Prop1";
    prop.type = QDBusType('s');
    prop.access = QDBusIntrospection::Property::Read;
    Interface2::propertyData << prop;
    Interface3::propertyData << prop;
    Interface4::propertyData << prop;
    prop.name = "Prop2";
    prop.access = QDBusIntrospection::Property::ReadWrite;
    Interface2::propertyData << prop;
    Interface3::propertyData << prop;
    Interface4::propertyData << prop;
}

void tst_QDBusAbstractAdaptor::methodCalls_data()
{
    QTest::addColumn<int>("nInterfaces");
    QTest::newRow("0") << 0;
    QTest::newRow("1") << 1;
    QTest::newRow("2") << 2;
    QTest::newRow("3") << 3;
    QTest::newRow("4") << 4;
}

void tst_QDBusAbstractAdaptor::methodCalls()
{
    QDBusConnection &con = QDBus::sessionBus();
    QVERIFY(con.isConnected());

    QDBusObject dobj = con.findObject(con.baseService(), "/");
    QVERIFY(dobj.isValid());

    //QDBusInterface empty(dobj, QString());
    QDBusInterface if1(dobj, "local.Interface1");
    QDBusInterface if2(dobj, "local.Interface2");
    QDBusInterface if3(dobj, "local.Interface3");
    QDBusInterface if4(dobj, "local.Interface4");

    // must fail: no object
    //QCOMPARE(empty.call("method").type(), QDBusMessage::ErrorMessage);
    QCOMPARE(if1.call("method").type(), QDBusMessage::ErrorMessage);

    QObject obj;
    con.registerObject("/", &obj);

    QFETCH(int, nInterfaces);
    switch (nInterfaces)
    {
    case 4:
        new Interface4(&obj);
    case 3:
        new Interface3(&obj);
    case 2:
        new Interface2(&obj);
    case 1:
        new Interface1(&obj);
    }

    // must fail: no such method
    QCOMPARE(if1.call("method").type(), QDBusMessage::ErrorMessage);
    if (!nInterfaces--)
        return;
    if (!nInterfaces--)
        return;

    // simple call: one such method exists
    QCOMPARE(if2.call("method").type(), QDBusMessage::ReplyMessage);
    QCOMPARE(slotSpy, "void Interface2::method()");
    if (!nInterfaces--)
        return;

    // multiple methods in multiple interfaces, no name overlap
    QCOMPARE(if1.call("methodVoid").type(), QDBusMessage::ErrorMessage);
    QCOMPARE(if1.call("methodInt").type(), QDBusMessage::ErrorMessage);
    QCOMPARE(if1.call("methodString").type(), QDBusMessage::ErrorMessage);
    QCOMPARE(if2.call("methodVoid").type(), QDBusMessage::ErrorMessage);
    QCOMPARE(if2.call("methodInt").type(), QDBusMessage::ErrorMessage);
    QCOMPARE(if2.call("methodString").type(), QDBusMessage::ErrorMessage);

    QCOMPARE(if3.call("methodVoid").type(), QDBusMessage::ReplyMessage);
    QCOMPARE(slotSpy, "void Interface3::methodVoid()");
    QCOMPARE(if3.call("methodInt", 42).type(), QDBusMessage::ReplyMessage);
    QCOMPARE(slotSpy, "void Interface3::methodInt(int)");
    QCOMPARE(if3.call("methodString", QString("")).type(), QDBusMessage::ReplyMessage);
    QCOMPARE(slotSpy, "void Interface3::methodString(QString)");

    if (!nInterfaces--)
        return;

    // method overloading: different interfaces
    QCOMPARE(if4.call("method").type(), QDBusMessage::ReplyMessage);
    QCOMPARE(slotSpy, "void Interface4::method()");

    // method overloading: different parameters
    QCOMPARE(if4.call("method.i", 42).type(), QDBusMessage::ReplyMessage);
    QCOMPARE(slotSpy, "void Interface4::method(int)");
    QCOMPARE(if4.call("method.s", QString()).type(), QDBusMessage::ReplyMessage);
    QCOMPARE(slotSpy, "void Interface4::method(QString)");
    
}

static void emitSignal(QDBusConnection &con, const QString &iface, const QString &name,
                       const QVariant &parameter)
{
    QObject obj;
    Interface2 *if2 = new Interface2(&obj);
    Interface3 *if3 = new Interface3(&obj);
    Interface4 *if4 = new Interface4(&obj);
    con.registerObject("/",&obj);

    if (iface.endsWith('2'))
        if2->emitSignal(name, parameter);
    else if (iface.endsWith('3'))
        if3->emitSignal(name, parameter);
    else if (iface.endsWith('4'))
        if4->emitSignal(name, parameter);
    
    QTest::qWait(200);
}

void tst_QDBusAbstractAdaptor::signalEmissions_data()
{
    QTest::addColumn<QString>("interface");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("signature");
    QTest::addColumn<QVariant>("parameter");

    QTest::newRow("Interface2.signal") << "local.Interface2" << "signal" << QString() << QVariant();
    QTest::newRow("Interface3.signalVoid") << "local.Interface3" << "signalVoid" << QString() << QVariant();
    QTest::newRow("Interface3.signalInt") << "local.Interface3" << "signalInt" << "i" << QVariant(1);
    QTest::newRow("Interface3.signalString") << "local.Interface3" << "signalString" << "s" << QVariant("foo");
}

void tst_QDBusAbstractAdaptor::signalEmissions()
{
    QFETCH(QString, interface);
    QFETCH(QString, name);
    QFETCH(QVariant, parameter);

    QDBusConnection &con = QDBus::sessionBus();
    QVERIFY(con.isConnected());

    QDBusObject dobj = con.findObject(con.baseService(), "/");
    QVERIFY(dobj.isValid());

    //QDBusInterface empty(dobj, QString());
    QDBusInterface if2(dobj, "local.Interface2");
    QDBusInterface if3(dobj, "local.Interface3");

    // connect all signals and emit only one
    {
        QDBusSignalSpy spy;
        if2.connect("signal", &spy, SLOT(slot(QDBusMessage)));
        if3.connect("signalVoid", &spy, SLOT(slot(QDBusMessage)));
        if3.connect("signalInt", &spy, SLOT(slot(QDBusMessage)));
        if3.connect("signalString", &spy, SLOT(slot(QDBusMessage)));
        
        emitSignal(con, interface, name, parameter);
        
        QCOMPARE(spy.count, 1);
        QCOMPARE(spy.interface, interface);
        QCOMPARE(spy.name, name);
        QTEST(spy.signature, "signature");
        QCOMPARE(spy.value, parameter);
    }

    // connect one signal and emit them all
    {
        QDBusSignalSpy spy;
        con.connect(con.baseService(), "/", interface, name, &spy, SLOT(slot(QDBusMessage)));
        emitSignal(con, "local.Interface2", "signal", QVariant());
        emitSignal(con, "local.Interface3", "signalVoid", QVariant());
        emitSignal(con, "local.Interface3", "signalInt", QVariant(1));
        emitSignal(con, "local.Interface3", "signalString", QVariant("foo"));
        
        QCOMPARE(spy.count, 1);
        QCOMPARE(spy.interface, interface);
        QCOMPARE(spy.name, name);
        QTEST(spy.signature, "signature");
        QCOMPARE(spy.value, parameter);
    }
}

void tst_QDBusAbstractAdaptor::sameSignalDifferentPaths()
{
    QDBusConnection &con = QDBus::sessionBus();
    QVERIFY(con.isConnected());

    QObject obj;
    Interface2 *if2 = new Interface2(&obj);

    con.registerObject("/p1",&obj);
    con.registerObject("/p2",&obj);

    QDBusSignalSpy spy;
    con.connect(con.baseService(), "/p1", "local.Interface2", "signal", &spy, SLOT(slot(QDBusMessage)));
    if2->emitSignal(QString(), QVariant());
    QTest::qWait(200);
    
    QCOMPARE(spy.count, 1);
    QCOMPARE(spy.interface, QString("local.Interface2"));
    QCOMPARE(spy.name, QString("signal"));
    QVERIFY(spy.signature.isEmpty());

    // now connect the other one
    spy.count = 0;
    con.connect(con.baseService(), "/p2", "local.Interface2", "signal", &spy, SLOT(slot(QDBusMessage)));
    if2->emitSignal(QString(), QVariant());
    QTest::qWait(200);
    
    QCOMPARE(spy.count, 2);
}

void tst_QDBusAbstractAdaptor::overloadedSignalEmission_data()
{
    QTest::addColumn<QString>("signature");
    QTest::addColumn<QVariant>("parameter");
    QTest::newRow("void") << QString("") << QVariant();
    QTest::newRow("int") << "i" << QVariant(1);
    QTest::newRow("string") << "s" << QVariant("foo");
}

void tst_QDBusAbstractAdaptor::overloadedSignalEmission()
{
    QDBusConnection &con = QDBus::sessionBus();
    QVERIFY(con.isConnected());

    QString interface = "local.Interface4";
    QString name = "signal";
    QFETCH(QVariant, parameter);
    QDBusInterface if4 = con.findInterface(con.baseService(), "/", interface);
    
    // connect all signals and emit only one
    {
        QDBusSignalSpy spy;
        if4.connect("signal.", &spy, SLOT(slot(QDBusMessage)));
        if4.connect("signal.i", &spy, SLOT(slot(QDBusMessage)));
        if4.connect("signal.s", &spy, SLOT(slot(QDBusMessage)));
        
        emitSignal(con, interface, name, parameter);
        
        QCOMPARE(spy.count, 1);
        QCOMPARE(spy.interface, interface);
        QCOMPARE(spy.name, name);
        QTEST(spy.signature, "signature");
        QCOMPARE(spy.value, parameter);
    }

    QFETCH(QString, signature);
    // connect one signal and emit them all
    {
        QDBusSignalSpy spy;
        con.connect(con.baseService(), "/", interface, name, signature, &spy, SLOT(slot(QDBusMessage)));
        emitSignal(con, "local.Interface4", "signal", QVariant());
        emitSignal(con, "local.Interface4", "signal", QVariant(1));
        emitSignal(con, "local.Interface4", "signal", QVariant("foo"));
        
        QCOMPARE(spy.count, 1);
        QCOMPARE(spy.interface, interface);
        QCOMPARE(spy.name, name);
        QTEST(spy.signature, "signature");
        QCOMPARE(spy.value, parameter);
    }
}    

void tst_QDBusAbstractAdaptor::readProperties()
{
    QDBusConnection &con = QDBus::sessionBus();
    QVERIFY(con.isConnected());

    QObject obj;
    new Interface2(&obj);
    new Interface3(&obj);
    new Interface4(&obj);
    con.registerObject("/", &obj);

    for (int i = 2; i <= 4; ++i) {
        QString name = QString("Interface%1").arg(i);
        QDBusInterface iface = con.findInterface(con.baseService(), "/", "local." + name);

        for (int j = 1; j <= 2; ++j) {
            QString propname = QString("prop%1").arg(j);
            QDBusVariant value = iface.property(propname);

            QVERIFY(value.type == QDBusType('s'));
            QVERIFY(value.value.type() == QVariant::String);
            QCOMPARE(value.value.toString(), QString("QString %1::%2() const").arg(name, propname));
        }
    }
}

void tst_QDBusAbstractAdaptor::writeProperties()
{
    QDBusConnection &con = QDBus::sessionBus();
    QVERIFY(con.isConnected());

    QObject obj;
    new Interface2(&obj);
    new Interface3(&obj);
    new Interface4(&obj);
    con.registerObject("/", &obj);

    for (int i = 2; i <= 4; ++i) {
        QString name = QString("Interface%1").arg(i);
        QDBusInterface iface = con.findInterface(con.baseService(), "/", "local." + name);

        QDBusVariant value(name);

        propSpy.clear();
        iface.setProperty("prop1", value);
        QVERIFY(propSpy.isEmpty()); // call mustn't have succeeded

        iface.setProperty("prop2", value);
        QCOMPARE(propSpy, name);
        QCOMPARE(QString(slotSpy), QString("void %1::setProp2(const QString&)").arg(name));
    }
}

void tst_QDBusAbstractAdaptor::adaptorIntrospection_data()
{
    methodCalls_data();
}

void tst_QDBusAbstractAdaptor::adaptorIntrospection()
{
    QDBusConnection &con = QDBus::sessionBus();
    QVERIFY(con.isConnected());

    QObject obj;
    con.registerObject("/", &obj);

    QFETCH(int, nInterfaces);
    switch (nInterfaces)
    {
    case 4:
        new Interface4(&obj);
    case 3:
        new Interface3(&obj);
    case 2:
        new Interface2(&obj);
    case 1:
        new Interface1(&obj);
    }

    QDBusObject dobj = con.findObject(con.baseService(), "/");
    QVERIFY(dobj.isValid());

    QString xml = dobj.introspect();
    QVERIFY(!xml.isEmpty());

    QStringList interfaces = dobj.interfaces();
    QCOMPARE(interfaces.count(), nInterfaces + 2);
    switch (nInterfaces)
    {
    case 4: {
        QVERIFY(interfaces.contains("local.Interface4"));
        QDBusInterface iface(dobj, "local.Interface4");
        QCOMPARE(iface.methodData(), Interface4::methodData);
        QCOMPARE(iface.signalData(), Interface4::signalData);
        QCOMPARE(iface.propertyData(), Interface4::propertyData);
    }
    case 3: {
        QVERIFY(interfaces.contains("local.Interface3"));
        QDBusInterface iface(dobj, "local.Interface3");
        QCOMPARE(iface.methodData(), Interface3::methodData);
        QCOMPARE(iface.signalData(), Interface3::signalData);
        QCOMPARE(iface.propertyData(), Interface3::propertyData);
    }
    case 2: {
        QVERIFY(interfaces.contains("local.Interface2"));
        QDBusInterface iface(dobj, "local.Interface2");
        QCOMPARE(iface.methodData(), Interface2::methodData);
        QCOMPARE(iface.signalData(), Interface2::signalData);
        QCOMPARE(iface.propertyData(), Interface2::propertyData);
    }
    case 1: {
        QVERIFY(interfaces.contains("local.Interface1"));
        QDBusInterface iface(dobj, "local.Interface1");
        QCOMPARE(iface.methodData(), Interface1::methodData);
        QCOMPARE(iface.signalData(), Interface1::signalData);
        QCOMPARE(iface.propertyData(), Interface1::propertyData);
    }
    }
}

void tst_QDBusAbstractAdaptor::objectTreeIntrospection()
{
    QDBusConnection &con = QDBus::sessionBus();
    QVERIFY(con.isConnected());

    {
        QDBusObject dobj = con.findObject(con.baseService(), "/");
        QString xml = dobj.introspect();

        QDBusIntrospection::Object tree =
            QDBusIntrospection::parseObject(xml);
        QVERIFY(tree.childObjects.isEmpty());
    }

    QObject root;
    con.registerObject("/", &root);
    {
        QDBusObject dobj = con.findObject(con.baseService(), "/");
        QString xml = dobj.introspect();

        QDBusIntrospection::Object tree =
            QDBusIntrospection::parseObject(xml);
        QVERIFY(tree.childObjects.isEmpty());
    }

    QObject p1;
    con.registerObject("/p1", &p1);
    {
        QDBusObject dobj = con.findObject(con.baseService(), "/");
        QString xml = dobj.introspect();

        QDBusIntrospection::Object tree =
            QDBusIntrospection::parseObject(xml);
        QVERIFY(tree.childObjects.contains("p1"));
    }

    con.unregisterObject("/");
    {
        QDBusObject dobj = con.findObject(con.baseService(), "/");
        QString xml = dobj.introspect();

        QDBusIntrospection::Object tree =
            QDBusIntrospection::parseObject(xml);
        QVERIFY(tree.childObjects.contains("p1"));
    }

    con.registerObject("/p1/q/r", &root);    
    {
        QDBusObject dobj = con.findObject(con.baseService(), "/p1");
        QString xml = dobj.introspect();

        QDBusIntrospection::Object tree =
            QDBusIntrospection::parseObject(xml);
        QVERIFY(tree.childObjects.contains("q"));
    }
    {
        QDBusObject dobj = con.findObject(con.baseService(), "/p1/q");
        QString xml = dobj.introspect();

        QDBusIntrospection::Object tree =
            QDBusIntrospection::parseObject(xml);
        QVERIFY(tree.childObjects.contains("r"));
    }

    con.unregisterObject("/p1", QDBusConnection::UnregisterTree);
    {
        QDBusObject dobj = con.findObject(con.baseService(), "/");
        QString xml = dobj.introspect();

        QDBusIntrospection::Object tree =
            QDBusIntrospection::parseObject(xml);
        QVERIFY(tree.childObjects.isEmpty());
    }

    QObject p2;
    con.registerObject("/p2", &p2, QDBusConnection::ExportChildObjects);
    {
        QDBusObject dobj = con.findObject(con.baseService(), "/");
        QString xml = dobj.introspect();

        QDBusIntrospection::Object tree =
            QDBusIntrospection::parseObject(xml);
        QVERIFY(!tree.childObjects.contains("p1"));
        QVERIFY(tree.childObjects.contains("p2"));
    }
    
    QObject q;
    q.setParent(&p2);
    {
        QDBusObject dobj = con.findObject(con.baseService(), "/p2");
        QString xml = dobj.introspect();

        QDBusIntrospection::Object tree =
            QDBusIntrospection::parseObject(xml);
        QVERIFY(!tree.childObjects.contains("q"));
    }

    q.setObjectName("q");
    {
        QDBusObject dobj = con.findObject(con.baseService(), "/p2");
        QString xml = dobj.introspect();

        QDBusIntrospection::Object tree =
            QDBusIntrospection::parseObject(xml);
        QVERIFY(tree.childObjects.contains("q"));
    }

    q.setParent(0);
    {
        QDBusObject dobj = con.findObject(con.baseService(), "/p2");
        QString xml = dobj.introspect();

        QDBusIntrospection::Object tree =
            QDBusIntrospection::parseObject(xml);
        QVERIFY(!tree.childObjects.contains("q"));
    }
}    

QTEST_MAIN(tst_QDBusAbstractAdaptor)

#include "tst_qdbusabstractadaptor.moc"

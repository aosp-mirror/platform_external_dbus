/* -*- mode: C++ -*-
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
 * along with this program; if not, write to the Free Software Foundation
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "qdbusabstractadaptor.h"

#include <QtCore/qmetaobject.h>
#include <QtCore/qtimer.h>

#include "qdbusconnection.h"

#include "qdbusconnection_p.h"  // for qDBusParametersForMethod
#include "qdbusabstractadaptor_p.h"

/*!
    \internal
*/
struct QDBusAdaptorInit
{
    QSignalSpyCallbackSet callbacks;
    QDBusAdaptorInit()
    {
        extern void qt_register_signal_spy_callbacks(const QSignalSpyCallbackSet &callback_set);
        callbacks.signal_begin_callback = QDBusAdaptorConnector::signalBeginCallback;
        callbacks.signal_end_callback = QDBusAdaptorConnector::signalEndCallback;
        callbacks.slot_begin_callback = 0;
        callbacks.slot_end_callback = 0;
        qt_register_signal_spy_callbacks(callbacks);
        
        //QDBusAdaptorConnector::id = QObject::registerUserData();
    }
};

Q_GLOBAL_STATIC(QDBusAdaptorInit, qAdaptorInit)

QDBusAdaptorConnector *qDBusFindAdaptorConnector(QObject *obj)
{
    qAdaptorInit();

#if 0
    if (caller->metaObject() == QDBusAdaptorConnector::staticMetaObject)
        return 0;               // it's a QDBusAdaptorConnector
#endif

    if (!obj)
        return 0;
    QDBusAdaptorConnector *connector = qFindChild<QDBusAdaptorConnector *>(obj);
    if (connector)
        connector->polish();
    return connector;
}

QDBusAdaptorConnector *qDBusCreateAdaptorConnector(QObject *obj)
{
    qAdaptorInit();

    QDBusAdaptorConnector *connector = qDBusFindAdaptorConnector(obj);
    if (connector)
        return connector;
    return new QDBusAdaptorConnector(obj);
}

QString QDBusAbstractAdaptorPrivate::retrieveIntrospectionXml(QDBusAbstractAdaptor *adaptor)
{
    return adaptor->d->xml;
}

void QDBusAbstractAdaptorPrivate::saveIntrospectionXml(QDBusAbstractAdaptor *adaptor,
                                                       const QString &xml)
{
    adaptor->d->xml = xml;
}

/*!
    \page UsingAdaptors Using Adaptors

    Adaptors are special classes that are attached to any QObject-derived class and provide the
    interface to the external world using D-Bus. Adaptors are intended to be light-weight classes
    whose main purpose is to relay calls to and from the real object, possibly validating or
    converting the input from the external world and, thus, protecting the real object.

    Unlike multiple inheritance, adaptors can be added at any time to any object (but not removed),
    which allows for greater flexibility when exporting existing classes. Another advantage of
    adaptors is to provide similar but not identical functionality in methods of the same name in
    different interfaces, a case which can be quite common when adding a new version of a standard
    interface to an object.

    In order to use an adaptor, one must create a class which inherits QDBusAbstractAdaptor. Since
    that is a standard QObject-derived class, the Q_OBJECT macro must appear in the declaration and
    the source file must be processed with the \link moc \endlink tool. The class must also contain
    one or more Q_CLASSINFO entries with the "D-Bus Interface" name, declaring which interfaces it
    is exporting.

    Any public slot in the class will be accessible through the bus over messages of the MethodCall
    type. (See \link DeclaringSlots \endlink for more information). Signals in the class will be
    automatically relayed over D-Bus. However, not all types are allowed signals or slots' parameter
    lists: see \link AllowedParameters \endlink for more information.

    Also, any property declared with Q_PROPERTY will be automatically exposed over the Properties
    interface on D-Bus. Since the QObject property system does not allow for non-readable
    properties, it is not possible to declare write-only properties using adaptors.

    More information:
    - \subpage DeclaringSlots
    - \subpage DeclaringSignals
    - \subpage AllowedParameters
    - \subpage UsingAnnotations
    - \subpage AdaptorExample

    \sa QDBusAbstractAdaptor
*/

/*!
    \page AdaptorExample Example of an interface implemented with an adaptor

    A sample usage of QDBusAbstractAdaptor is as follows:
    \code
        class MainApplicationAdaptor: public QDBusAbstractAdaptor
        {
            Q_OBJECT
            Q_CLASSINFO("D-Bus Interface", "com.example.DBus.MainApplication")
            Q_CLASSINFO("D-Bus Interface", "org.kde.DBus.MainApplication")
            Q_PROPERTY(QString caption READ caption WRITE setCaption)
            Q_PROPERTY(QString organizationName READ organizationName)
            Q_PROPERTY(QString organizationDomain READ organizationDomain)

        private:
            MyApplication *app;

        public:
            MyInterfaceAdaptor(MyApplication *application)
                : QDBusAbstractAdaptor(application), app(application)
            {
                connect(application, SIGNAL(aboutToQuit()), SIGNAL(aboutToQuit());
                connect(application, SIGNAL(focusChanged(QWidget*, QWidget*)),
                        SLOT(focusChangedSlot(QWidget*, QWidget*)));
            }

            QString caption()
            {
                if (app->hasMainWindow())
                    return app->mainWindow()->caption();
                return QString();
            }

            void setCaption(const QString &newCaption)
            {
                if (app->hasMainWindow())
                    app->mainWindow()->setCaption(newCaption);
            }

            QString organizationName()
            {
                return app->organizationName();
            }

            QString organizationDomain()
            {
                return app->organizationDomain();
            }

        public slots:
            async void quit()
            { app->quit(); }

            void reparseConfiguration()
            { app->reparseConfiguration(); }

            QString mainWindowObject()
            {
                if (app->hasMainWindow())
                    return QString("/%1/mainwindow").arg(app->applicationName());
                return QString();
            }

            void setSessionManagement(bool enable)
            {
                if (enable)
                   app->enableSessionManagement();
                else
                   app->disableSessionManagement();
            }

        private slots:
            void focusChangedSlot(QWidget *, QWidget *now)
            {
                if (now == app->mainWindow())
                    emit mainWindowHasFocus();
            }

        signals:
            void aboutToQuit();
            void mainWindowHasFocus();
        };
    \endcode

    The code above would create an interface that could be represented more or less in the following
    canonical representation:
    \code
        interface com.example.DBus.MainApplication
        {
            property readwrite STRING caption
            property read STRING organizationName
            property read STRING organizationDomain

            method quit() annotation("org.freedesktop.DBus.Method.NoReply", "true")
            method reparseConfiguration()
            method mainWindowObject(out STRING)
            method disableSessionManagement(in BOOLEAN enable)

            signal aboutToQuit()
            signal mainWindowHasFocus()
        }

        interface org.kde.DBus.MainApplication
        {
            ....
        }
    \endcode

    This adaptor could be used in the application's constructor as follows:
    \code
        MyApplication::MyApplication()
        {
            [...]

            // create the MainApplication adaptor:
            new MainApplicationAdaptor(this);

            // connect to D-Bus:
            QDBusConnection connection = QDBusConnection::addConnection(QDBusConnection::SessionBus);

            // register us as an object:
            connection.registerObject("/MainApplication", this);

            [...]
        }
    \endcode
            
    Break-down analysis:
    - \subpage AdaptorExampleHeader
    - \subpage AdaptorExampleProperties
    - \subpage AdaptorExampleConstructor
    - \subpage AdaptorExampleSlots
    - \subpage AdaptorExampleSignals
*/

/*!
    \page AdaptorExampleHeader The header

    The header of the example is:
    \code
        class MainApplicationAdaptor: public QDBusAbstractAdaptor
        {
            Q_OBJECT
            Q_CLASSINFO("D-Bus Interface", "com.example.DBus.MainApplication")
            Q_CLASSINFO("D-Bus Interface", "org.kde.DBus.MainApplication")
    \endcode

    The code does the following:
    - it declares the adaptor MainApplicationAdaptor, which descends from QDBusAbstractAdaptor
    - it declares the Qt Meta Object data using the #Q_OBJECT macro
    - it declares the names of two D-Bus interfaces it implements. Those interfaces are equal in all
      aspects.
*/

/*!
    \page AdaptorExampleProperties The properties

    The properties are declared as follows:
    \code
            Q_PROPERTY(QString caption READ caption WRITE setCaption)
            Q_PROPERTY(QString organizationName READ organizationName)
            Q_PROPERTY(QString organizationDomain READ organizationDomain)
    \endcode

    And are implemented as follows:
    \code
            QString caption()
            {
                if (app->hasMainWindow())
                    return app->mainWindow()->caption();
                return QString();
            }

            void setCaption(const QString &newCaption)
            {
                if (app->hasMainWindow())
                    app->mainWindow()->setCaption(newCaption);
            }

            QString organizationName()
            {
                return app->organizationName();
            }

            QString organizationDomain()
            {
                return app->organizationDomain();
            }
    \endcode

    The code declares three properties: one of them is a read-write property called "caption" of
    string type. The other two are read-only, also of the string type.

    The properties organizationName and organizationDomain are simple relays of the app object's
    organizationName and organizationDomain properties. However, the caption property requires
    verifying if the application has a main window associated with it: if there isn't any, the
    caption property is empty. Note how it is possible to access data defined in other objects
    through the getter/setter functions.
 */

/*!
    \page AdaptorExampleConstructor The constructor

    The constructor:
    \code
            MyInterfaceAdaptor(MyApplication *application)
                : QDBusAbstractAdaptor(application), app(application)
            {
                connect(application, SIGNAL(aboutToQuit()), SIGNAL(aboutToQuit());
                connect(application, SIGNAL(focusChanged(QWidget*, QWidget*)),
                        SLOT(focusChangedSlot(QWidget*, QWidget*)));
            }
    \endcode

    The constructor does the following:
    - it initialises its base class (QDBusAbstractAdaptor) with the parent object it is related to.
    - it stores the app pointer in a member variable. Note that it would be possible to access the
      same object using the QDBusAbstractAdaptor::object() function, but it would be necessary to
      use \a static_cast<> to properly access the methods in MyApplication that are not part of
      QObject.
    - it connects the application's signal \a aboutToQuit to its own signal \a aboutToQuit.
    - it connects the application's signal \a focusChanged to a private slot to do some further
      processing before emitting a D-Bus signal.

    Note that there is no destructor in the example. An eventual destructor could be used to emit
    one last signal before the object is destroyed, for instance.
*/

/*!
    \page AdaptorExampleSlots Slots/methods

    The public slots in the example (which will be exported as D-Bus methods) are the following:
    \code
        public slots:
            async void quit()
            { app->quit(); }

            void reparseConfiguration()
            { app->reparseConfiguration(); }

            QString mainWindowObject()
            {
                if (app->hasMainWindow())
                    return QString("/%1/mainwindow").arg(app->applicationName());
                return QString();
            }

            void setSessionManagement(bool enable)
            {
                if (enable)
                   app->enableSessionManagement();
                else
                   app->disableSessionManagement();
            }
    \endcode

    This snippet of code defines 4 methods with different properties each:
    - \p quit: this method takes no parameters and is defined to be asynchronous. That is, callers
      are expected to use "fire-and-forget" mechanism when calling this method, since it provides no
      useful reply. This is represented in D-Bus by the use of the
      org.freedesktop.DBus.Method.NoReply annotation. See #Q_ASYNC for more information on
      asynchronous methods

    - \p reparseConfiguration: this simple method, with no input or output arguments simply relays
      the call to the application's reparseConfiguration member function.

    - \p mainWindowObject: this method takes no input parameter, but returns one string output
      argument, containing the path to the main window object (if the application has a main
      window), or an empty string if it has no main window. Note that this method could have also
      been written: void mainWindowObject(QString &path).

    - \p setSessionManagement: this method takes one input argument (a boolean) and, depending on
      its value, it calls one function or another in the application.

    \sa #Q_ASYNC
*/

/*!
    \page AdaptorExampleSignals Signals

    The signals in this example are defined as follows:
    \code
        signals:
            void aboutToQuit();
            void mainWindowHasFocus();
    \endcode

    However, signal definition isn't enough: signals have to be emitted. One simple way of emitting
    signals is to connect another signal to them, so that Qt's signal handling system chains them
    automatically. This is what is done for the \a aboutToQuit signal (see \ref
    AdaptorExampleConstructor).

    When this is the case, one can use the QDBusAbstractAdaptor::setAutoRelaySignals to
    automatically connect every signal from the real object to the adaptor.

    When simple signal-to-signal connection isn't enough, one can use a private slot do do some
    work. This is what was done for the mainWindowHasFocus signal:
    \code
        private slots:
            void focusChangedSlot(QWidget *, QWidget *now)
            {
                if (now == app->mainWindow())
                    emit mainWindowHasFocus();
            }
    \endcode

    This private slot (which will not be exported as a method via D-Bus) was connected to the
    \a focusChanged signal in the adaptor's constructor. It is therefore able to shape the
    application's signal into what the interface expects it to be.
*/

/*!
    \page DeclaringSlots Declaring slots

    Slots in D-Bus adaptors are declared just like normal, public slots, but their parameters must
    follow certain rules (see \ref AllowedParameters for more information). Slots whose parameters
    do not follow those rules or that are not public will not be accessible via D-Bus.

    Slots can be of three kinds:
    -# Asynchronous
    -# Input-only
    -# Input-and-output

    \par Asynchronous slots
         Asynchronous slots are those that do not normally return any reply to the caller. For that
         reason, they cannot take any output parameters. In most cases, by the time the first line
         of the slot is run, the caller function has already resumed working.

    \par
         However, slots must rely on that behavior. Scheduling and message-dispatching issues could
         change the order in which the slot is run. Code intending to synchronize with the caller
         should provide its own method of synchronization.

    \par
         Asynchronous slots are marked by the keyword \p #async or \p #Q_ASYNC in the method
         signature, before the \p void return type and the slot name. (See the \p quit slot in the
         \ref AdaptorExample "adaptor example").

    \par Input-only slots
         Input-only slots are normal slots that take parameters passed by value or by constant
         reference. However, unlike asynchronous slots, the caller is usually waiting for completion
         of the callee before resuming operation. Therefore, non-asynchronous slots should not block
         or should state it its documentation that they may do so.

    \par
         Input-only slots have no special marking in their signature, except that they take only
         parameters passed by value or by constant reference. Optionally, slots can take a
         QDBusMessage parameter as a last parameter, which can be used to perform additional
         analysis of the method call message.

    \par Input and output slots
         Like input-only slots, input-and-output slots are those that the caller is waiting for a
         reply. Unlike input-only ones, though, this reply will contain data. Slots that output data
         may contain non-constant references and may return a value as well. However, the output
         parameters must all appear at the end of the argument list and may not have input arguments
         interleaved. Optionally, a QDBusMessage argument may appear between the input and the
         output arguments.

    \note When a caller places a method call and waits for a reply, it will only wait for so long.
         Slots intending to take a long time to complete should make that fact clear in
         documentation so that callers properly set higher timeouts.

    Method replies are generated automatically with the contents of the output parameters (if there
    were any) by the QtDBus implementation. Slots need not worry about constructing proper
    QDBusMessage objects and sending them over the connection.

    However, the possibility of doing so remains there. Should the slot find out it needs to send a
    special reply or even an error, it can do so by using QDBusMessage::methodReply or
    QDBusMessage::error on the QDBusMessage parameter and send it with QDBusConnection::send. The
    QtDBus implementation will not generate any reply if the slot did so.

    \sa \ref UsingAdaptors, \ref DeclaringSignals, \ref AllowedParameters, QDBusConnection,
        QDBusMessage
*/

/*!
    \page DeclaringSignals Declaring signals

    Any signal in a class derived from QDBusAbstractAdaptor will be automatically relayed into
    D-Bus, provided that the signal's parameters conform to certain rules (see \ref
    AllowedParameters for more information). No special code is necessary to make this relay.

    However, signals must still be emitted. The easiest way to emit an adaptor signal is to connect
    another signal to it, so that the Qt signal/slot mechanism automatically emits the adaptor
    signal too. This can be done in the adaptor's constructor, as has been done in the \ref
    AdaptorExample "adaptor example".

    The convenience function QDBusAbstractAdaptor::setAutoRelaySignals can also be used to connect
    or disconnect every signal in the real object to the same signal in the adaptor. It will inspect
    the list of signals in both classes and connect those that have exact parameter match.

    \sa \ref UsingAdaptors, \ref DeclaringSlots, \ref AllowedParameters, QDBusAbstractAdaptor
*/

/*!
    \page AllowedParameters Allowed parameter types

    D-Bus has a very limited set of types that can be sent and received over the bus. They are
    listed below, along with the D-Bus type they relate to:
    - unsigned char / uchar (BYTE)
    - short (INT16)
    - unsigned short / ushort (UINT16)
    - int (INT32)
    - unsigned int / uint (UINT32)
    - qlonglong (INT64)
    - qulonglong (UINT64)
    - bool (BOOLEAN)
    - double (DOUBLE)
    - QString (STRING)
    - QByteArray (ARRAY of BYTE)
    - QStringList (ARRAY of STRING)
    - QVariant / QDBusVariant (VARIANT)
    - QVariantList (ARRAY of VARIANT)
    - QVariantMap (ARRAY of DICT_ENTRY of (STRING, VARIANT))

    The last two types may be used to receive any array (except string and byte arrays), any structs
    and any maps. However, it is currently not possible to generate external function definitions
    containing specific types of lists, structs and maps.

    All of the types above may be passed by value or by constant reference for input arguments to
    slots as well as the output arguments to signals. When used as output arguments for slots, they
    can all be used as non-constant references or the return type.

    Additionally, slots can have one parameter of type \p const \p QDBusMessage \p \&, which must
    appear at the end of the input parameter list, before any output parameters. Signals cannot have
    this parameter.

    \warning You may not use any type that is not on the list above, including \a typedefs to the
    types listed. This also includes QList<QVariant> and QMap<QString,QVariant>.
*/

/*!
    \page UsingAnnotations Using annotations in adaptors

    It is currently not possible to specify arbitrary annotations in adaptors.
*/

/*!
    \class QDBusAbstractAdaptor
    \brief Abstract adaptor for D-Bus adaptor classes.

    The QDBusAbstractAdaptor class is the starting point for all objects intending to provide
    interfaces to the external world using D-Bus. This is accomplished by attaching a one or more
    classes derived from QDBusAbstractAdaptor to a normal QObject and then registering that QObject
    with QDBusConnection::registerObject. QDBusAbstractAdaptor objects are intended to be
    light-weight wrappers, mostly just relaying calls into the real object (see object()) and the
    signals from it.

    Each QDBusAbstractAdaptor-derived class should define the D-Bus interface it is implementing
    using the Q_CLASSINFO macro in the class definition.

    QDBusAbstractAdaptor uses the standard QObject mechanism of signals, slots and properties to
    determine what signals, methods and properties to export to the bus. Any signal emitted by
    QDBusAbstractAdaptor-derived classes will be automatically be relayed through any D-Bus
    connections the object is registered on.

    Classes derived from QDBusAbstractAdaptor must be created on the heap using the \a new operator
    and must not be deleted by the user (they will be deleted automatically when the object they are
    connected to is also deleted).

    \sa \ref UsingAdaptors, QDBusConnection
*/

/*!
    Constructs a QDBusAbstractAdaptor with \a parent as the object we refer to.

    \param parent       the real object we're the adaptor for

    \warning Use object() to retrieve the object passed as \a parent to this constructor. The real
             parent object (as retrieved by QObject::parent()) may be something else.
*/
QDBusAbstractAdaptor::QDBusAbstractAdaptor(QObject* parent)
    : d(new QDBusAbstractAdaptorPrivate)
{
    QDBusAdaptorConnector *connector = qDBusCreateAdaptorConnector(parent);
    setParent(connector);

    connector->waitingForPolish = true;
    QTimer::singleShot(0, connector, SLOT(polish()));
}

/*!
    Destroys the adaptor.

    \warning Adaptors are destroyed automatically when the real object they refer to is
             destroyed. Do not delete the adaptors yourself.
*/
QDBusAbstractAdaptor::~QDBusAbstractAdaptor()
{
    delete d;
}

/*!
    Returns the QObject that we're the adaptor for. This is the same object that was passed as an
    argument to the QDBusAbstractAdaptor constructor.
*/
QObject* QDBusAbstractAdaptor::object() const
{
    return parent()->parent();
}

/*!
    Toggles automatic signal relaying from the real object (see object()).

    Automatic signal relaying consists of signal-to-signal connection of the signals on the parent
    that have the exact same method signatue in both classes.

    \param enable       if set to true, connect the signals; if set to false, disconnect all signals
*/
void QDBusAbstractAdaptor::setAutoRelaySignals(bool enable)
{
    const QMetaObject *us = metaObject();
    const QMetaObject *them = parent()->metaObject();
    for (int idx = staticMetaObject.methodCount(); idx < us->methodCount(); ++idx) {
        QMetaMethod mm = us->method(idx);

        if (mm.methodType() != QMetaMethod::Signal)
            continue;
        
        // try to connect/disconnect to a signal on the parent that has the same method signature
        QByteArray sig = QMetaObject::normalizedSignature(mm.signature());
        if (them->indexOfSignal(sig) == -1)
            continue;
        sig.prepend(QSIGNAL_CODE + '0');
        object()->disconnect(sig, this, sig);
        if (enable)
            connect(object(), sig, sig, Qt::QueuedConnection);
    }
}

QDBusAdaptorConnector::QDBusAdaptorConnector(QObject *parent)
    : QObject(parent), waitingForPolish(false), lastSignalIdx(0), argv(0)
{
}

QDBusAdaptorConnector::~QDBusAdaptorConnector()
{
}

void QDBusAdaptorConnector::addAdaptor(QDBusAbstractAdaptor *adaptor)
{
    // find the interface name
    const QMetaObject *mo = adaptor->metaObject();
    while (mo != &QDBusAbstractAdaptor::staticMetaObject) {
        int ciend = mo->classInfoCount();
        for (int i = mo->classInfoOffset(); i < ciend; ++i) {
            QMetaClassInfo mci = mo->classInfo(i);
            if (strcmp(mci.name(), QCLASSINFO_DBUS_INTERFACE) == 0 && *mci.value()) {
                // find out if this interface exists first
                QString interface = QString::fromUtf8(mci.value());
                AdaptorMap::Iterator it = qLowerBound(adaptors.begin(), adaptors.end(), interface);
                if (it != adaptors.end() && it->interface == interface) {
                    // exists. Replace it (though it's probably the same)
                    it->adaptor = adaptor;
                    it->metaObject = mo;
                } else {
                    // create a new one
                    AdaptorData entry;
                    entry.interface = interface;
                    entry.adaptor = adaptor;
                    entry.metaObject = mo;
                    adaptors << entry;
                }
            }
        }

        mo = mo->superClass();
    }
        
    // connect the adaptor's signals to our relaySlot slot
    mo = adaptor->metaObject();
    for (int i = QDBusAbstractAdaptor::staticMetaObject.methodCount();
         i < mo->methodCount(); ++i) {
        QMetaMethod mm = mo->method(i);

        if (mm.methodType() != QMetaMethod::Signal)
            continue;

        QByteArray sig = mm.signature();
        sig.prepend(QSIGNAL_CODE + '0');
        disconnect(adaptor, sig, this, SLOT(relaySlot()));
        connect(adaptor, sig, this, SLOT(relaySlot()));
    }
}

void QDBusAdaptorConnector::polish()
{
    if (!waitingForPolish)
        return;                 // avoid working multiple times if multiple adaptors were added

    waitingForPolish = false;
    const QObjectList &objs = children();
    foreach (QObject *obj, objs) {
        Q_ASSERT(qobject_cast<QDBusAbstractAdaptor *>(obj));

        QDBusAbstractAdaptor *adaptor = static_cast<QDBusAbstractAdaptor *>(obj);
        addAdaptor(adaptor);
    }

    // sort the adaptor list
    qSort(adaptors);
}

void QDBusAdaptorConnector::relaySlot()
{
    relay(sender());
}

void QDBusAdaptorConnector::relay(QObject *sender)
{
    // we're being called because there is a signal being emitted that we must relay
    Q_ASSERT(lastSignalIdx);
    Q_ASSERT(argv);
    Q_ASSERT(senderMetaObject);

    if (senderMetaObject != sender->metaObject()) {
        qWarning("Inconsistency detected: QDBusAdaptorConnector::relay got called with unexpected sender object!");
    } else {
        QMetaMethod mm = senderMetaObject->method(lastSignalIdx);
        QObject *object = static_cast<QDBusAbstractAdaptor *>(sender)->object();

        // break down the parameter list
        QList<int> types;
        QByteArray signature = QMetaObject::normalizedSignature(mm.signature());
        int inputCount = qDBusParametersForMethod(signature, types);
        if (inputCount == -1)
            // invalid signal signature
            // qDBusParametersForMethod has already complained
            return;
        if (inputCount + 1 != types.count() ||
            types.at(inputCount) == QDBusConnectionPrivate::messageMetaType) {
            // invalid signal signature
            // qDBusParametersForMethod has not yet complained about this one
            qWarning("Cannot relay signal %s::%s", senderMetaObject->className(), mm.signature());
            return;
        }

        signature.truncate(signature.indexOf('(')); // remove parameter decoration

        QVariantList args;
        for (int i = 1; i < types.count(); ++i)
            args << QVariant(types.at(i), argv[i]);

        // find all the interfaces this signal belongs to
        for (const QMetaObject *mo = senderMetaObject; mo != &QDBusAbstractAdaptor::staticMetaObject;
             mo = mo->superClass()) {
            if (lastSignalIdx < mo->methodOffset())
                break;

            for (int i = mo->classInfoOffset(); i < mo->classInfoCount(); ++i) {
                QMetaClassInfo mci = mo->classInfo(i);
                if (qstrcmp(mci.name(), QCLASSINFO_DBUS_INTERFACE) == 0 && *mci.value())
                    // now emit the signal with all the information
                    emit relaySignal(object, mci.value(), signature.constData(), args);
            }
        }
    }
}

void QDBusAdaptorConnector::signalBeginCallback(QObject *caller, int method_index, void **argv)
{
    QDBusAdaptorConnector *data = qobject_cast<QDBusAdaptorConnector *>(caller->parent());
    if (data) {
        data->lastSignalIdx = method_index;
        data->argv = argv;
        data->senderMetaObject = caller->metaObject();
        data->polish();         // make sure it's polished
    }
}

void QDBusAdaptorConnector::signalEndCallback(QObject *caller, int)
{
    QDBusAdaptorConnector *data = qobject_cast<QDBusAdaptorConnector *>(caller->parent());
    if (data) {
        data->lastSignalIdx = 0;
        data->argv = 0;
        data->senderMetaObject = 0;
    }
}

#include "qdbusabstractadaptor.moc"
#include "qdbusabstractadaptor_p.moc"

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

#ifndef QDBUSXMLPARSER_H
#define QDBUSXMLPARSER_H

#include <QtCore/qmap.h>
#include <QtXml/qdom.h>
#include "qdbusmacros.h"
#include "qdbusintrospection.h"

class QDBusConnectionPrivate;
class QDBusObjectPrivate;

/**
 * @internal
 */
class QDBusXmlParser
{
    QString m_service;
    QString m_path;
    QDomElement m_node;
    QDBusConnectionPrivate* m_store;

public:
    QDBusXmlParser(const QString& service, const QString& path,
                   const QString& xmlData, QDBusConnectionPrivate* store = 0);
    QDBusXmlParser(const QString& service, const QString& path,
                   const QDomElement& node, QDBusConnectionPrivate* store = 0);

    QDBusIntrospection::Interfaces interfaces() const;
    QSharedDataPointer<QDBusIntrospection::Object> object() const;
    QSharedDataPointer<QDBusIntrospection::ObjectTree> objectTree() const;

    static void parse(const QDBusObjectPrivate* d, const QString &xml);
};

#endif

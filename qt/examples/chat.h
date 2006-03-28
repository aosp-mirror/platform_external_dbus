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

#ifndef CHAT_H
#define CHAT_H

#include <QtCore/QStringList>
#include <dbus/qdbus.h>
#include "chatmainwindow.h"
#include "chatsetnickname.h"

class ChatMainWindow: public QMainWindow, Ui::ChatMainWindow
{
    Q_OBJECT
    QString m_nickname;
    QStringList m_messages;
public:
    ChatMainWindow();
    ~ChatMainWindow();

    void rebuildHistory();

signals:
    void message(const QString &nickname, const QString &text);
    void action(const QString &nickname, const QString &text);

private slots:
    void messageSlot(const QString &nickname, const QString &text);
    void actionSlot(const QString &nickname, const QString &text);
    void textChangedSlot(const QString &newText);
    void sendClickedSlot();
    void changeNickname();
    void aboutQt();
    void exiting();
};

class NicknameDialog: public QDialog, public Ui::NicknameDialog
{
    Q_OBJECT
public:
    NicknameDialog(QWidget *parent = 0);
};

#endif

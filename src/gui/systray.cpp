/*
 * Copyright (C) by Cédric Bellegarde <gnumdk@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "systray.h"
#include "theme.h"
#include "config.h"
#include <QDebug>

#ifdef USE_FDO_NOTIFICATIONS
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#define NOTIFICATIONS_SERVICE "org.freedesktop.Notifications"
#define NOTIFICATIONS_PATH "/org/freedesktop/Notifications"
#define NOTIFICATIONS_IFACE "org.freedesktop.Notifications"
#endif

namespace OCC {

void Systray::showMessage(const QString &title, const QString &message, const QStringList &actions, MessageIcon icon, int millisecondsTimeoutHint)
{
#ifdef USE_FDO_NOTIFICATIONS
    if(QDBusInterface(NOTIFICATIONS_SERVICE, NOTIFICATIONS_PATH, NOTIFICATIONS_IFACE).isValid()) {
        foreach (QString action, actions) {
            qDebug() << "ACTION:" << action;
        }
        qDebug() << "MESSAGE:" << message;
        QList<QVariant> args = QList<QVariant>() << APPLICATION_NAME << quint32(0) << APPLICATION_ICON_NAME
                                                 << title << message << actions << QVariantMap() << qint32(-1);
        QDBusMessage method = QDBusMessage::createMethodCall(NOTIFICATIONS_SERVICE, NOTIFICATIONS_PATH, NOTIFICATIONS_IFACE,
                                                             "Notify");
        QDBusConnection::sessionBus().connect(NOTIFICATIONS_SERVICE, NOTIFICATIONS_PATH, NOTIFICATIONS_IFACE,
                                          "ActionInvoked", this, SLOT(slotActionInvoked(uint,QString)));
        method.setArguments(args);
        QDBusConnection::sessionBus().asyncCall(method);
        QDBusMessage method2 = QDBusMessage::createMethodCall(NOTIFICATIONS_SERVICE, NOTIFICATIONS_PATH, NOTIFICATIONS_IFACE,
                                                             "GetCapabilities");
        QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(method2);
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);

        connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
                 this, SLOT(asyncCallFinished(QDBusPendingCallWatcher*)));
    } else
#endif
#ifdef Q_OS_OSX
        if (canOsXSendUserNotification()) {
        sendOsXUserNotification(title, message);
    } else
#endif
    {
        QSystemTrayIcon::showMessage(title, message, icon, millisecondsTimeoutHint);
    }
}

void Systray::asyncCallFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusMessage m = watcher->reply();
    qDebug() << "asyncCallFinished" << m;
}

void Systray::slotActionInvoked(uint id, QString action_key)
{
    emit actionInvoked(id, action_key);
}

void Systray::setToolTip(const QString &tip)
{
    QSystemTrayIcon::setToolTip(tr("%1: %2").arg(Theme::instance()->appNameGUI(), tip));
}

} // namespace OCC

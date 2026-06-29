// SPDX-FileCopyrightText: 2025 Matias Fernandez <matias.fernandez@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef UNITYLAUNCHERWATCHER_H
#define UNITYLAUNCHERWATCHER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

// Listens for the Unity LauncherEntry DBus API
// (com.canonical.Unity.LauncherEntry.Update), the de-facto convention apps
// like Telegram and Thunderbird/Betterbird use to publish unread counts to
// taskbars. There's no Wayland protocol for this — it's a session-bus
// broadcast from the app itself, so we connect with an empty service and
// path to catch it regardless of sender.
class UnityLauncherWatcher : public QObject
{
    Q_OBJECT
public:
    explicit UnityLauncherWatcher(QObject *parent = nullptr);

Q_SIGNALS:
    // desktopId has no "application://" prefix or ".desktop" suffix, e.g.
    // "org.telegram.desktop" — ready to compare against an Item's appId or
    // a launcher's desktop file basename.
    void badgeChanged(const QString &desktopId, int count, bool visible);

private Q_SLOTS:
    void onUpdate(const QString &appUri, const QVariantMap &properties);

private:
    struct State {
        int count = 0;
        bool visible = false;
    };
    QHash<QString, State> m_state;
};

#endif

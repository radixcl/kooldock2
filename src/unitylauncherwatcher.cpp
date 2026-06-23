// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "unitylauncherwatcher.h"

#include <QDBusConnection>

UnityLauncherWatcher::UnityLauncherWatcher(QObject *parent)
    : QObject(parent)
{
    QDBusConnection::sessionBus().connect(
        QString(), QString(), QStringLiteral("com.canonical.Unity.LauncherEntry"),
        QStringLiteral("Update"), this, SLOT(onUpdate(QString,QVariantMap)));
}

void UnityLauncherWatcher::onUpdate(const QString &appUri, const QVariantMap &properties)
{
    // app_uri looks like "application://org.telegram.desktop.desktop".
    QString id = appUri;
    static const QString prefix = QStringLiteral("application://");
    if (id.startsWith(prefix)) id.remove(0, prefix.length());
    if (id.endsWith(QStringLiteral(".desktop"))) id.chop(8);
    if (id.isEmpty()) return;

    // The spec only sends properties that changed, so merge into what we
    // already know about this app rather than resetting it.
    State &state = m_state[id];
    if (properties.contains(QStringLiteral("count")))
        state.count = properties.value(QStringLiteral("count")).toInt();
    if (properties.contains(QStringLiteral("count-visible")))
        state.visible = properties.value(QStringLiteral("count-visible")).toBool();

    Q_EMIT badgeChanged(id, state.count, state.visible);
}

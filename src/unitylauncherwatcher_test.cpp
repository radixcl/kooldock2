// SPDX-License-Identifier: GPL-2.0-or-later

#include "unitylauncherwatcher.h"

#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    UnityLauncherWatcher watcher;
    QString id;
    int count = -1;
    bool visible = false;
    QObject::connect(&watcher, &UnityLauncherWatcher::badgeChanged,
                     [&](const QString &desktopId, int n, bool shown) {
        id = desktopId;
        count = n;
        visible = shown;
    });
    const QVariantMap update{{QStringLiteral("count"), 7},
                             {QStringLiteral("count-visible"), true}};
    QMetaObject::invokeMethod(&watcher, "onUpdate", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("application://org.example.App.desktop")),
                              Q_ARG(QVariantMap, update));
    count = -1;
    watcher.replayBadges();
    return id == QStringLiteral("org.example.App") && count == 7 && visible ? 0 : 1;
}

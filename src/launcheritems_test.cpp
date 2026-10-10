// SPDX-License-Identifier: GPL-2.0-or-later

#include "launcheritems.h"
#include "item.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

int main(int argc, char **argv)
{
    QTemporaryDir home;
    if (!home.isValid()) return 1;
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kooldock2-launcher-test"));
    LauncherItems launchers;

    auto writeLauncher = [&](const QString &path, const QString &name, bool hidden = false) {
        QFile desktop(path);
        if (!desktop.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        desktop.write("[Desktop Entry]\nType=Application\nName=" + name.toUtf8()
                      + "\nExec=true\n" + (hidden ? "NoDisplay=true\n" : ""));
        return true;
    };
    if (!writeLauncher(launchers.menuDir() + QStringLiteral("00-other.desktop"), QStringLiteral("Other"))
        || !writeLauncher(launchers.menuDir() + QStringLiteral("10-hidden.desktop"), QStringLiteral("Hidden Teams"), true)
        || !writeLauncher(launchers.menuDir() + QStringLiteral("20-teams.desktop"), QStringLiteral("Teams"))) return 2;

    auto names = [&]() {
        QStringList result;
        const auto items = launchers.load();
        for (Item *item : items) result.append(item->name());
        qDeleteAll(items);
        return result;
    };

    if (names() != QStringList({QStringLiteral("Other"), QStringLiteral("Teams")})) return 3;
    launchers.moveLauncher(1, 0);
    if (names() != QStringList({QStringLiteral("Teams"), QStringLiteral("Other")})) return 4;
    launchers.removeLauncher(0);
    if (names() != QStringList({QStringLiteral("Other")})) return 5;
    if (launchers.sortedFiles().size() != 2) return 6; // Hidden file was not removed.
    const QString source = home.filePath(QStringLiteral("third.desktop"));
    if (!writeLauncher(source, QStringLiteral("Third"))) return 7;
    if (!launchers.addLauncherAt(source, 0)) return 8;
    if (names() != QStringList({QStringLiteral("Third"), QStringLiteral("Other")})) return 8;
    if (launchers.addLauncherAt(home.filePath(QStringLiteral("missing.desktop")), 0)) return 9;
    if (names() != QStringList({QStringLiteral("Third"), QStringLiteral("Other")})) return 10;
    return 0;
}

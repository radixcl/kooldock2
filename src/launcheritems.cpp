// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "launcheritems.h"

#include "item.h"

#include <KDesktopFile>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

LauncherItems::LauncherItems(QObject *parent)
    : QObject(parent)
{
    m_menuDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/menu/");
    QDir().mkpath(m_menuDir);
}

QString LauncherItems::menuDir() const
{
    return m_menuDir;
}

QList<Item *> LauncherItems::load() const
{
    QList<Item *> result;
    QDir dir(m_menuDir);
    if (!dir.exists()) {
        return result;
    }

    const QStringList files = dir.entryList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        ensureDefaultLaunchers();
    }

    for (const QString &file : files) {
        const QString path = dir.absoluteFilePath(file);
        KDesktopFile df(path);
        const QString name = df.readName();
        const QString icon = df.readIcon();
        const QString exec = df.desktopGroup().readEntry(QStringLiteral("Exec"), QString());
        const bool noDisplay = df.noDisplay();

        if (noDisplay || name.isEmpty()) {
            continue;
        }

        auto *item = new Item(Item::Kind::Launcher, name, icon, exec, 0);
        item->setDesktopFile(path);
        result.append(item);
    }
    return result;
}

void LauncherItems::refresh()
{
    Q_EMIT changed();
}

void LauncherItems::addLauncher(const QString &desktopFile)
{
    Q_UNUSED(desktopFile);
    Q_EMIT changed();
}

void LauncherItems::removeLauncher(const QString &desktopFile)
{
    QFile::remove(desktopFile);
    Q_EMIT changed();
}

void LauncherItems::ensureDefaultLaunchers() const
{
    const QString marker = m_menuDir + QStringLiteral(".defaults-created");
    if (QFile::exists(marker)) {
        return;
    }

    struct DefaultLauncher {
        QString fileName;
        QString name;
        QString exec;
        QString icon;
    };

    const QList<DefaultLauncher> defaults = {
        {QStringLiteral("10-filemanager.desktop"), QStringLiteral("File Manager"), QStringLiteral("dolphin"), QStringLiteral("system-file-manager")},
        {QStringLiteral("20-terminal.desktop"), QStringLiteral("Terminal"), QStringLiteral("konsole"), QStringLiteral("utilities-terminal")},
        {QStringLiteral("30-browser.desktop"), QStringLiteral("Web Browser"), QStringLiteral("firefox"), QStringLiteral("internet-web-browser")},
        {QStringLiteral("40-settings.desktop"), QStringLiteral("System Settings"), QStringLiteral("systemsettings5"), QStringLiteral("preferences-system")},
    };

    for (const auto &launcher : defaults) {
        const QString path = m_menuDir + launcher.fileName;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            continue;
        }

        QTextStream out(&file);
        out << QStringLiteral("[Desktop Entry]\n");
        out << QStringLiteral("Name=") << launcher.name << QStringLiteral("\n");
        out << QStringLiteral("Exec=") << launcher.exec << QStringLiteral("\n");
        out << QStringLiteral("Icon=") << launcher.icon << QStringLiteral("\n");
        out << QStringLiteral("Type=Application\n");
        out << QStringLiteral("Terminal=false\n");
        file.close();
    }

    QFile markerFile(marker);
    if (markerFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        markerFile.close();
    }
}

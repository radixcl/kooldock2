// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "launcheritems.h"

#include "item.h"

#include <KDesktopFile>
#include <KConfigGroup>

#include <QDir>
#include <QStandardPaths>

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

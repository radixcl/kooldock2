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
    // Copy the .desktop file into the dock's menu directory with a
    // high numeric prefix so it lands at the end of the sort order.
    const QFileInfo srcInfo(desktopFile);
    const QString baseName = srcInfo.completeBaseName();
    const QString suffix = srcInfo.suffix().isEmpty() ? QStringLiteral("desktop") : srcInfo.suffix();

    const QStringList existing = sortedFiles();
    int nextNum = 90;
    for (const QString &f : existing) {
        const int prefix = f.section(QLatin1Char('-'), 0, 0).toInt();
        if (prefix >= nextNum) nextNum = prefix + 10;
    }
    const QString dest = m_menuDir + QStringLiteral("%1-%2.%3").arg(nextNum, 2, 10, QLatin1Char('0')).arg(baseName, suffix);
    if (QFile::exists(dest)) QFile::remove(dest);
    if (!QFile::copy(desktopFile, dest)) {
        // Fall back to writing a minimal .desktop from scratch if copy
        // fails (e.g. source not readable).
        return;
    }
    Q_EMIT changed();
}

void LauncherItems::removeLauncher(int index)
{
    const QStringList files = sortedFiles();
    if (index < 0 || index >= files.size()) return;
    QFile::remove(m_menuDir + files.at(index));
    renumber(files.mid(0, index) + files.mid(index + 1));
    Q_EMIT changed();
}

void LauncherItems::moveLauncher(int from, int to)
{
    const QStringList files = sortedFiles();
    if (from < 0 || from >= files.size() || to < 0 || to >= files.size() || from == to) return;

    QStringList reordered = files;
    reordered.move(from, to);
    renumber(reordered);
    Q_EMIT changed();
}

QStringList LauncherItems::sortedFiles() const
{
    QDir dir(m_menuDir);
    return dir.entryList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name);
}

void LauncherItems::renumber(const QStringList &orderedPaths)
{
    // Rename each file with a sequential 00, 10, 20, ... prefix so the
    // QDir::Name sort order matches the desired launcher order. Rename
    // to temp names first to avoid collisions during the shuffle.
    const QString tmpSuffix = QStringLiteral(".tmp");
    int n = orderedPaths.size();
    // Phase 1: rename all to temp names.
    for (int i = 0; i < n; i++) {
        const QString oldPath = m_menuDir + orderedPaths.at(i);
        const QString tmpPath = oldPath + tmpSuffix;
        if (QFile::exists(tmpPath)) QFile::remove(tmpPath);
        QFile::rename(oldPath, tmpPath);
    }
    // Phase 2: rename from temp to final numbered names.
    for (int i = 0; i < n; i++) {
        const QString baseName = orderedPaths.at(i).section(QLatin1Char('-'), 1);
        const QString tmpPath = m_menuDir + orderedPaths.at(i) + tmpSuffix;
        const QString newPath = m_menuDir + QStringLiteral("%1-%2").arg(i * 10, 2, 10, QLatin1Char('0')).arg(baseName);
        QFile::rename(tmpPath, newPath);
    }
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

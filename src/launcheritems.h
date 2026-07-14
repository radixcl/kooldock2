// SPDX-FileCopyrightText: 2025 Matias Fernandez <matias.fernandez@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef LAUNCHERITEMS_H
#define LAUNCHERITEMS_H

#include <QHash>
#include <QObject>
#include <QStringList>

class Item;

class LauncherItems : public QObject
{
    Q_OBJECT
public:
    explicit LauncherItems(QObject *parent = nullptr);

    QList<Item *> load() const;

    QString menuDir() const;

    // Launcher file names (sans dir) in on-disk order.
    QStringList sortedFiles() const;

    public Q_SLOTS:
    void refresh();
    void addLauncher(const QString &desktopFile);
    // Add a launcher at a specific position in the order (index < 0 appends).
    void addLauncherAt(const QString &desktopFile, int index);
    void removeLauncher(int index);
    void moveLauncher(int from, int to);

Q_SIGNALS:
    void changed();

private:
    void ensureDefaultLaunchers() const;
    // Copy a .desktop into the menu dir with a high (end-of-order) prefix and
    // tag its source; returns the dest filename (sans dir) or empty on failure.
    QString copyLauncherFile(const QString &desktopFile) const;
    void renumber(const QStringList &orderedPaths);

    QString m_menuDir;
};

#endif

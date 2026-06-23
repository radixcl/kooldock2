// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
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

    public Q_SLOTS:
    void refresh();
    void addLauncher(const QString &desktopFile);
    void removeLauncher(int index);
    void moveLauncher(int from, int to);

Q_SIGNALS:
    void changed();

private:
    void ensureDefaultLaunchers() const;
    QStringList sortedFiles() const;
    void renumber(const QStringList &orderedPaths);

    QString m_menuDir;
};

#endif

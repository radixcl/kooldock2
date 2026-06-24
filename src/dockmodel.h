// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOCKMODEL_H
#define DOCKMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QVariantList>

class Item;
class LauncherItems;
class UnityLauncherWatcher;

#include "windowtasks.h"

class DockModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)

public:
    explicit DockModel(WindowTasks *tasks, bool debug = false, QObject *parent = nullptr);
    ~DockModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    QVariantList items() const;

    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void activateWindow(quint64 windowId);
    Q_INVOKABLE void activateSpecificWindow(quint64 windowId);
    Q_INVOKABLE void launch(int row);
    Q_INVOKABLE void newWindow(int row);
    Q_INVOKABLE QVariantMap itemData(int row) const;
    Q_INVOKABLE QVariantList windowListForRow(int row) const;
    Q_INVOKABLE void addLauncher(const QString &filePath);
    Q_INVOKABLE void removeLauncher(int row);
    Q_INVOKABLE void moveLauncher(int from, int to);
    Q_INVOKABLE bool isLauncher(int row) const;
    Q_INVOKABLE void pinTask(quint64 windowId);
    // Per-app "Desktop Actions" ([Desktop Action X] groups in the .desktop
    // file, e.g. Firefox/LibreWolf's "New Private Window") — queried fresh
    // each time the context menu opens, not part of the persistent model.
    Q_INVOKABLE QVariantList desktopActions(int row) const;
    Q_INVOKABLE void triggerDesktopAction(int row, const QString &actionId);

public Q_SLOTS:
    void reload();
    void onWindowAdded(quint64 windowId);
    void onWindowRemoved(quint64 windowId);
    void onWindowChanged(quint64 windowId);
    void onActiveWindowChanged(quint64 windowId);

Q_SIGNALS:
    void countChanged();
    void itemsChanged();
    void itemRemoved(int row);
    void itemChanged(int row);
    void itemInserted(int row);
    void itemMoved(int from, int to);
    void activateAppMenu();
    void activateTrash();

private Q_SLOTS:
    void onLaunchersChanged();
    void onBadgeChanged(const QString &desktopId, int count, bool visible);

private:
    void rebuild();
    void updateIndices();
    int insertTaskSorted(Item *item);
    bool shouldShowTask(const WindowTasks::TaskData &data) const;
    Item *findLauncherForAppId(const QString &appId) const;
    Item *findTaskForAppId(const QString &appId) const;
    QString desktopPathForAppId(const QString &appId) const;
    QString desktopFileForRow(int row) const;

    QList<Item *> m_items;
    QHash<quint64, Item *> m_taskItems;
    LauncherItems *m_launchers;
    UnityLauncherWatcher *m_launcherWatcher;
    WindowTasks *m_tasks = nullptr;
    quint64 m_activeWindow = 0;
    quint64 m_lastActivatedWindow = 0;
    bool m_suppressReload = false;
    bool m_loading = false;
    bool m_debug = false;
};

#endif

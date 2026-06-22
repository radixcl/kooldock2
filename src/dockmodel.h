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

class DockModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)

public:
    explicit DockModel(QObject *parent = nullptr);
    ~DockModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    QVariantList items() const;

    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void activateWindow(quint64 windowId);
    Q_INVOKABLE void launch(int row);
    Q_INVOKABLE QVariantMap itemData(int row) const;

public Q_SLOTS:
    void reload();
    void onWindowAdded(quint64 windowId);
    void onWindowRemoved(quint64 windowId);
    void onWindowChanged(quint64 windowId);
    void onActiveWindowChanged(quint64 windowId);

Q_SIGNALS:
    void countChanged();
    void itemsChanged();

private Q_SLOTS:
    void onLaunchersChanged();

private:
    void rebuild();
    void updateIndices();
    int insertTaskSorted(Item *item);

    QList<Item *> m_items;
    QHash<quint64, Item *> m_tasks;
    LauncherItems *m_launchers;
    quint64 m_activeWindow = 0;
};

#endif

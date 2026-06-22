// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dockmodel.h"

#include "item.h"
#include "kooldocksettings.h"
#include "launcheritems.h"

#include <KDesktopFile>
#include <KConfigGroup>
#include <KWindowInfo>
#include <KWindowSystem>
#include <KX11Extras>
#include <KIO/ApplicationLauncherJob>
#include <KIO/CommandLauncherJob>

DockModel::DockModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_launchers(new LauncherItems(this))
{
    connect(m_launchers, &LauncherItems::changed, this, &DockModel::onLaunchersChanged);
    rebuild();
}

DockModel::~DockModel()
{
    qDeleteAll(m_items);
}

int DockModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariantList DockModel::items() const
{
    QVariantList list;
    list.reserve(m_items.size());
    for (auto *item : m_items) {
        list.append(QVariant::fromValue(static_cast<QObject *>(item)));
    }
    return list;
}

int DockModel::count() const { return m_items.size(); }

void DockModel::activate(int row)
{
    if (row < 0 || row >= m_items.size()) return;
    Item *item = m_items.at(row);
    if (item->isTask()) {
        activateWindow(item->windowId());
    } else {
        launch(row);
    }
}

void DockModel::activateWindow(quint64 windowId)
{
    if (!windowId || !KWindowSystem::isPlatformX11()) return;
    if (windowId == m_activeWindow) {
        KX11Extras::minimizeWindow(static_cast<WId>(windowId));
    } else {
        KX11Extras::activateWindow(static_cast<WId>(windowId));
    }
}

void DockModel::launch(int row)
{
    if (row < 0 || row >= m_items.size()) return;
    Item *item = m_items.at(row);
    if (!item->isLauncher()) return;

    const QString desktopFile = item->desktopFile();
    if (!desktopFile.isEmpty()) {
        KDesktopFile df(desktopFile);
        KService::Ptr service = KService::serviceByDesktopPath(desktopFile);
        if (service) {
            auto *job = new KIO::ApplicationLauncherJob(service);
            job->setUiDelegate(nullptr);
            job->start();
            return;
        }
        const QString exec = df.desktopGroup().readEntry(QStringLiteral("Exec"), QString());
        if (!exec.isEmpty()) {
            auto *job = new KIO::CommandLauncherJob(exec);
            job->setUiDelegate(nullptr);
            job->start();
            return;
        }
    }
    if (!item->command().isEmpty()) {
        auto *job = new KIO::CommandLauncherJob(item->command());
        job->setUiDelegate(nullptr);
        job->start();
    }
}

void DockModel::reload()
{
    m_items.clear();
    m_tasks.clear();
    m_items = m_launchers->load();

    if (KoolDockSettings::showTaskbar() && KWindowSystem::isPlatformX11()) {
        const QList<WId> windows = KX11Extras::windows();
        for (WId wid : windows) {
            onWindowAdded(static_cast<quint64>(wid));
        }
        m_activeWindow = static_cast<quint64>(KX11Extras::activeWindow());
    }
    updateIndices();

    beginResetModel();
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT itemsChanged();
}

void DockModel::updateIndices()
{
    for (int i = 0; i < m_items.size(); ++i) {
        m_items[i]->setItemIndex(i);
    }
}

int DockModel::insertTaskSorted(Item *item)
{
    int firstTask = 0;
    while (firstTask < m_items.size() && m_items.at(firstTask)->isLauncher()) {
        ++firstTask;
    }
    beginInsertRows({}, firstTask, firstTask);
    m_items.insert(firstTask, item);
    endInsertRows();
    updateIndices();
    Q_EMIT countChanged();
    Q_EMIT itemsChanged();
    return firstTask;
}

void DockModel::onWindowAdded(quint64 windowId)
{
    if (!KoolDockSettings::showTaskbar() || !KWindowSystem::isPlatformX11()) return;
    if (m_tasks.contains(windowId)) return;

    const KWindowInfo info(static_cast<WId>(windowId),
                           NET::WMWindowType | NET::WMState | NET::WMName | NET::WMVisibleName,
                           NET::WM2WindowClass);
    if (!info.valid()) return;
    const auto types = NET::NormalMask | NET::DialogMask | NET::UtilityMask;
    if (!(info.windowType(types) == NET::Normal || info.windowType(types) == NET::Dialog
          || info.windowType(types) == NET::Utility)) return;
    if (info.state() & NET::SkipTaskbar) return;
    if (KoolDockSettings::ignoreList().contains(info.name())) return;
    if (KoolDockSettings::currentDesktopOnly() &&
        info.desktop() != KX11Extras::currentDesktop() && info.desktop() != NET::OnAllDesktops) return;
    if (KoolDockSettings::minimizedOnly() && !info.isMinimized()) return;

    auto *item = new Item(Item::Kind::Task, info.visibleName(), QString(), QString(), windowId);
    item->setMinimized(info.isMinimized());
    m_tasks.insert(windowId, item);
    insertTaskSorted(item);
}

void DockModel::onWindowRemoved(quint64 windowId)
{
    if (!m_tasks.contains(windowId)) return;
    Item *item = m_tasks.take(windowId);
    const int row = m_items.indexOf(item);
    if (row >= 0) {
        beginRemoveRows({}, row, row);
        m_items.removeAt(row);
        endRemoveRows();
        updateIndices();
        Q_EMIT countChanged();
        Q_EMIT itemsChanged();
    }
    delete item;
}

void DockModel::onWindowChanged(quint64 windowId)
{
    if (!m_tasks.contains(windowId) || !KWindowSystem::isPlatformX11()) return;
    Item *item = m_tasks.value(windowId);
    const KWindowInfo info(static_cast<WId>(windowId), NET::WMName | NET::WMVisibleName | NET::WMState);
    if (!info.valid()) return;
    item->setName(info.visibleName());
    item->setMinimized(info.isMinimized());
    item->setActive(windowId == m_activeWindow);

    if (KoolDockSettings::currentDesktopOnly() &&
        info.desktop() != KX11Extras::currentDesktop() && info.desktop() != NET::OnAllDesktops) {
        onWindowRemoved(windowId);
        return;
    }

    const int row = m_items.indexOf(item);
    if (row >= 0) Q_EMIT dataChanged(index(row), index(row));
}

void DockModel::onActiveWindowChanged(quint64 windowId)
{
    if (m_activeWindow == windowId) return;
    quint64 old = m_activeWindow;
    m_activeWindow = windowId;
    if (old && m_tasks.contains(old)) {
        m_tasks[old]->setActive(false);
        int r = m_items.indexOf(m_tasks[old]);
        if (r >= 0) Q_EMIT dataChanged(index(r), index(r));
    }
    if (windowId && m_tasks.contains(windowId)) {
        m_tasks[windowId]->setActive(true);
        int r = m_items.indexOf(m_tasks[windowId]);
        if (r >= 0) Q_EMIT dataChanged(index(r), index(r));
    }
}

void DockModel::onLaunchersChanged() { reload(); }

void DockModel::rebuild() { reload(); }

QVariant DockModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid()) return {};
    Item *item = m_items.at(idx.row());
    switch (role) {
    case Qt::DisplayRole: return item->name();
    case Qt::DecorationRole: return item->iconName();
    default: return {};
    }
}

QVariantMap DockModel::itemData(int row) const
{
    if (row < 0 || row >= m_items.size()) return {};
    Item *item = m_items.at(row);
    return {
        {QStringLiteral("name"), item->name()},
        {QStringLiteral("iconName"), item->iconName()},
        {QStringLiteral("isTask"), item->isTask()},
        {QStringLiteral("windowId"), item->windowId()},
        {QStringLiteral("itemIndex"), item->itemIndex()},
    };
}

QHash<int, QByteArray> DockModel::roleNames() const
{
    return {{Qt::DisplayRole, "display"}, {Qt::DecorationRole, "decoration"}};
}

// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dockmodel.h"

#include "item.h"
#include "kooldocksettings.h"
#include "launcheritems.h"
#include "windowtasks.h"

#include <KDesktopFile>
#include <KConfigGroup>
#include <KWindowInfo>
#include <KWindowSystem>
#include <KX11Extras>
#include <KIO/ApplicationLauncherJob>
#include <KIO/CommandLauncherJob>

#include <QDebug>

DockModel::DockModel(WindowTasks *tasks, QObject *parent)
    : QAbstractListModel(parent)
    , m_launchers(new LauncherItems(this))
    , m_tasks(tasks)
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
    if (!windowId || !m_tasks) return;

    const WindowTasks::TaskData data = m_tasks->taskData(windowId);
    if (data.active) {
        m_tasks->requestMinimize(windowId);
    } else {
        m_tasks->requestActivate(windowId);
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
    m_taskItems.clear();
    m_items = m_launchers->load();

    if (KoolDockSettings::showTaskbar() && m_tasks && m_tasks->isAvailable()) {
        for (quint64 wid : m_tasks->windowIds()) {
            onWindowAdded(wid);
        }
        m_activeWindow = m_tasks->activeWindow();
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

bool DockModel::shouldShowTask(const WindowTasks::TaskData &data) const
{
    if (data.skipTaskbar) {
        qDebug() << "shouldShowTask: rejected skipTaskbar" << data.title;
        return false;
    }
    if (data.skipSwitcher) {
        qDebug() << "shouldShowTask: rejected skipSwitcher" << data.title;
        return false;
    }
    if (KoolDockSettings::ignoreList().contains(data.title)) {
        qDebug() << "shouldShowTask: rejected ignoreList" << data.title;
        return false;
    }
    if (KoolDockSettings::minimizedOnly() && !data.minimized) {
        qDebug() << "shouldShowTask: rejected minimizedOnly" << data.title;
        return false;
    }
    if (KoolDockSettings::currentDesktopOnly() && KWindowSystem::isPlatformX11() &&
        !data.onAllDesktops && data.desktop != KX11Extras::currentDesktop()) {
        qDebug() << "shouldShowTask: rejected currentDesktopOnly" << data.title;
        return false;
    }

    // On X11 we keep the original window type filter. On Wayland the
    // compositor already filters what it exposes through the protocol, so we
    // accept everything that isn't explicitly skipped.
    if (KWindowSystem::isPlatformX11()) {
        const KWindowInfo info(static_cast<WId>(data.windowId),
                               NET::WMWindowType);
        if (!info.valid()) {
            return false;
        }
        const auto types = NET::NormalMask | NET::DialogMask | NET::UtilityMask;
        if (!(info.windowType(types) == NET::Normal || info.windowType(types) == NET::Dialog
              || info.windowType(types) == NET::Utility)) {
            return false;
        }
    }

    return true;
}

void DockModel::onWindowAdded(quint64 windowId)
{
    qDebug() << "DockModel::onWindowAdded id=" << windowId;
    if (!KoolDockSettings::showTaskbar() || !m_tasks || !m_tasks->isAvailable()) {
        qDebug() << "  rejected: showTaskbar=" << KoolDockSettings::showTaskbar()
                 << "tasks=" << m_tasks << "available=" << (m_tasks ? m_tasks->isAvailable() : false);
        return;
    }
    if (m_taskItems.contains(windowId)) {
        qDebug() << "  already tracked";
        return;
    }

    const WindowTasks::TaskData data = m_tasks->taskData(windowId);
    qDebug() << "  title=" << data.title << "icon=" << data.iconName
             << "skipTaskbar=" << data.skipTaskbar << "skipSwitcher=" << data.skipSwitcher
             << "minimized=" << data.minimized << "active=" << data.active;
    if (!shouldShowTask(data)) return;

    auto *item = new Item(Item::Kind::Task, data.title, data.iconName, QString(), windowId);
    item->setMinimized(data.minimized);
    item->setActive(data.active);
    m_taskItems.insert(windowId, item);
    insertTaskSorted(item);
    qDebug() << "  inserted, count=" << m_items.size();
}

void DockModel::onWindowRemoved(quint64 windowId)
{
    if (!m_taskItems.contains(windowId)) return;
    Item *item = m_taskItems.take(windowId);
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
    if (!m_taskItems.contains(windowId) || !m_tasks) return;

    Item *item = m_taskItems.value(windowId);
    const WindowTasks::TaskData data = m_tasks->taskData(windowId);

    if (!shouldShowTask(data)) {
        onWindowRemoved(windowId);
        return;
    }

    item->setName(data.title);
    item->setIconName(data.iconName);
    item->setMinimized(data.minimized);
    item->setActive(data.active);

    const int row = m_items.indexOf(item);
    if (row >= 0) Q_EMIT dataChanged(index(row), index(row));
}

void DockModel::onActiveWindowChanged(quint64 windowId)
{
    if (m_activeWindow == windowId) return;
    quint64 old = m_activeWindow;
    m_activeWindow = windowId;
    if (old && m_taskItems.contains(old)) {
        m_taskItems[old]->setActive(false);
        int r = m_items.indexOf(m_taskItems[old]);
        if (r >= 0) Q_EMIT dataChanged(index(r), index(r));
    }
    if (windowId && m_taskItems.contains(windowId)) {
        m_taskItems[windowId]->setActive(true);
        int r = m_items.indexOf(m_taskItems[windowId]);
        if (r >= 0) Q_EMIT dataChanged(index(r), index(r));
    }
}

void DockModel::onLaunchersChanged() { reload(); }

void DockModel::rebuild() { reload(); }

void DockModel::addLauncher(const QString &filePath)
{
    m_launchers->addLauncher(filePath);
}

void DockModel::removeLauncher(int row)
{
    if (row < 0 || row >= m_items.size()) return;
    if (!m_items.at(row)->isLauncher()) return;
    // Count only launchers up to this row to get the launcher index.
    int launcherIdx = 0;
    for (int i = 0; i < row; i++) {
        if (m_items.at(i)->isLauncher()) launcherIdx++;
    }
    m_launchers->removeLauncher(launcherIdx);
}

void DockModel::moveLauncher(int from, int to)
{
    if (from < 0 || from >= m_items.size()) return;
    if (!m_items.at(from)->isLauncher()) return;
    // Map model rows to launcher-only indices.
    int fromLauncher = 0;
    for (int i = 0; i < from; i++) {
        if (m_items.at(i)->isLauncher()) fromLauncher++;
    }
    // Clamp 'to' to the launcher range and map it.
    int lastLauncher = -1;
    for (int i = 0; i < m_items.size(); i++) {
        if (m_items.at(i)->isLauncher()) lastLauncher = i;
    }
    if (to > lastLauncher) to = lastLauncher;
    if (to < 0 || !m_items.at(to)->isLauncher()) return;
    int toLauncher = 0;
    for (int i = 0; i < to; i++) {
        if (m_items.at(i)->isLauncher()) toLauncher++;
    }
    m_launchers->moveLauncher(fromLauncher, toLauncher);
}

bool DockModel::isLauncher(int row) const
{
    if (row < 0 || row >= m_items.size()) return false;
    return m_items.at(row)->isLauncher();
}

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

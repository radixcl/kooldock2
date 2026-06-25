// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dockmodel.h"

#include "item.h"
#include "kooldocksettings.h"
#include "launcheritems.h"
#include "unitylauncherwatcher.h"
#include "windowtasks.h"

#include <KDesktopFile>
#include <KConfigGroup>
#include <KService>
#include <KWindowInfo>
#include <KWindowSystem>
#include <KX11Extras>
#include <KIO/ApplicationLauncherJob>
#include <KIO/CommandLauncherJob>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QRegularExpression>

// Strip freedesktop.org field codes (%u, %U, %f, %F, %% etc.)
// from an Exec= line. KIO::CommandLauncherJob does not expand these,
// and file/URL codes are N/A when launching from a dock.
static QString stripExecFieldCodes(const QString &exec)
{
    QString s = exec;
    // Remove single-URL and single-file placeholders (no URL/file in dock context)
    s.remove(QRegularExpression(QStringLiteral("%[uUfF]")));
    // Replace %% with literal %
    s.replace(QStringLiteral("%%"), QStringLiteral("%"));
    return s.trimmed();
}
#include <QStandardPaths>

DockModel::DockModel(WindowTasks *tasks, bool debug, QObject *parent)
    : QAbstractListModel(parent)
    , m_launchers(new LauncherItems(this))
    , m_launcherWatcher(new UnityLauncherWatcher(this))
    , m_tasks(tasks)
    , m_debug(debug)
{
    connect(m_launchers, &LauncherItems::changed, this, &DockModel::onLaunchersChanged);
    connect(m_launcherWatcher, &UnityLauncherWatcher::badgeChanged, this, &DockModel::onBadgeChanged);
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
    // AppMenu: show the KDE application launcher via DBus.
    if (item->isAppMenu()) {
        if (m_tasks) {
            // Defer to KoolDock via the tasks parent — but we don't have
            // a direct reference. Use QMetaObject::invokeMethod through
            // the model's parent chain. Simpler: emit a signal.
        }
        Q_EMIT activateAppMenu();
        return;
    }
    // Trash: open the trash folder.
    if (item->isTrash()) {
        Q_EMIT activateTrash();
        return;
    }
    // A launcher with a running task (fused) behaves like the task:
    // click toggles minimize/activate instead of launching a new instance.
    if (item->isLauncher() && item->isRunning() && item->windowId()) {
        activateWindow(item->windowId());
    } else if (item->isTask()) {
        activateWindow(item->windowId());
    } else {
        launch(row);
    }
}

void DockModel::activateWindow(quint64 windowId)
{
    if (!windowId || !m_tasks) return;

    Item *item = m_taskItems.value(windowId);

    // Grouped icons (multiple windows): always cycle to the next window
    if (item && item->isGrouped()) {
        const quint64 nextId = item->nextWindowId();
        if (nextId && nextId != windowId) {
            if (m_debug) qDebug() << "  activateWindow: cycling from" << windowId << "to" << nextId << "(grouped, count=" << item->windowCount() << ")";
            item->setPrimaryWindowId(nextId);
            m_tasks->requestActivate(nextId);
            return;
        }
    }

    const WindowTasks::TaskData data = m_tasks->taskData(windowId);

    // Plasma-taskbar behavior: clicking the currently-focused, non-minimized
    // window minimizes it; clicking any other window (or a minimized one)
    // raises it. m_activeWindow follows the compositor's real focus
    // (PlasmaWindowManagement::activeWindowChanged), so this stays correct
    // even after the focus moved elsewhere via Alt+Tab -- using the
    // last-activated-from-the-dock window here instead would wrongly minimize
    // a window that is no longer in front. While "Show Desktop" is active,
    // always activate: nothing is really shown, so minimizing would do
    // nothing visible and leave the dock looking unresponsive.
    if (windowId == m_activeWindow && !data.minimized && !KWindowSystem::showingDesktop()) {
        if (m_debug) qDebug() << "  activateWindow: minimizing" << windowId;
        m_tasks->requestMinimize(windowId);
    } else {
        if (m_debug) qDebug() << "  activateWindow: activating" << windowId << (data.minimized ? "(was minimized)" : "");
        m_tasks->requestActivate(windowId);
    }
}

void DockModel::activateSpecificWindow(quint64 windowId)
{
    if (!windowId || !m_tasks) return;

    Item *item = m_taskItems.value(windowId);
    if (item && item->isGrouped()) {
        item->setPrimaryWindowId(windowId);
    }

    const WindowTasks::TaskData data = m_tasks->taskData(windowId);
    if (windowId == m_activeWindow && !data.minimized && !KWindowSystem::showingDesktop()) {
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
        // Prefer sycoca-cached KService; falls back on an uncached one
        // built directly from the .desktop file so that ApplicationLauncherJob
        // can expand freedesktop.org field codes (%u, %U, %f, %F, %i, …).
        KService::Ptr service = KService::serviceByDesktopPath(desktopFile);
        if (!service) {
            service.reset(new KService(desktopFile));
        }
        if (service && service->isValid()) {
            auto *job = new KIO::ApplicationLauncherJob(service);
            job->setUiDelegate(nullptr);
            job->start();
            return;
        }
        // Last resort: the file may be malformed; read Exec= raw and strip
        // file/URL field codes that KIO::CommandLauncherJob cannot expand.
        KDesktopFile df(desktopFile);
        const QString exec = df.desktopGroup().readEntry(QStringLiteral("Exec"), QString());
        if (!exec.isEmpty()) {
            auto *job = new KIO::CommandLauncherJob(stripExecFieldCodes(exec));
            job->setUiDelegate(nullptr);
            job->start();
            return;
        }
    }
    if (!item->command().isEmpty()) {
        auto *job = new KIO::CommandLauncherJob(stripExecFieldCodes(item->command()));
        job->setUiDelegate(nullptr);
        job->start();
    }
}

void DockModel::newWindow(int row)
{
    // Launch a new instance of the launcher at `row`, even if it's fused
    // with a running task. Used by the "New Window" context menu item.
    if (row < 0 || row >= m_items.size()) return;
    Item *item = m_items.at(row);
    if (!item->isLauncher()) return;
    launch(row);
}

void DockModel::reload()
{
    // onWindowAdded() below can emit the granular itemInserted/itemChanged
    // signals as a side effect (fusing or inserting a task) — while m_items
    // is only partially rebuilt here, that's signals (and the eventual
    // itemsChanged trailer's partialUpdateHandled suppression) referring to
    // rows that don't match the final, post-reload state yet. Block them
    // until the rebuild below is complete and this function emits its own.
    m_loading = true;

    m_items.clear();
    m_taskItems.clear();

    // AppMenu (KDE application launcher) as the first item, if enabled.
    if (KoolDockSettings::showKMenu()) {
        auto *appMenuItem = new Item(Item::Kind::AppMenu,
            QStringLiteral("Applications"), QStringLiteral("start-here-kde"),
            QString(), 0);
        m_items.append(appMenuItem);
    }

    m_items += m_launchers->load();

    if (KoolDockSettings::showTaskbar() && m_tasks && m_tasks->isAvailable()) {
        for (quint64 wid : m_tasks->windowIds()) {
            onWindowAdded(wid);
        }
        m_activeWindow = m_tasks->activeWindow();
    }
    updateIndices();

    // Trash bin at the end, macOS-style.
    auto *trashItem = new Item(Item::Kind::Trash,
        QStringLiteral("Trash"), QStringLiteral("user-trash"),
        QString(), 0);
    m_items.append(trashItem);

    m_loading = false;

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
    while (firstTask < m_items.size() && (m_items.at(firstTask)->isLauncher() || m_items.at(firstTask)->isAppMenu())) {
        ++firstTask;
    }
    // Insert before the trash item (always last).
    int insertAt = firstTask;
    while (insertAt < m_items.size() && m_items.at(insertAt)->isTask()) {
        ++insertAt;
    }
    // If the item at insertAt is the trash, insert before it.
    if (insertAt < m_items.size() && m_items.at(insertAt)->isTrash()) {
        // insert before trash
    } else if (insertAt >= m_items.size()) {
        // No trash, insert at end
    }
    beginInsertRows({}, insertAt, insertAt);
    m_items.insert(insertAt, item);
    endInsertRows();
    updateIndices();
    if (!m_loading) {
        Q_EMIT itemInserted(insertAt);
        Q_EMIT countChanged();
        Q_EMIT itemsChanged();
    }
    return insertAt;
}

bool DockModel::shouldShowTask(const WindowTasks::TaskData &data) const
{
    if (data.skipTaskbar) {
        if (m_debug) qDebug() << "shouldShowTask: rejected skipTaskbar" << data.title;
        return false;
    }
    if (data.skipSwitcher) {
        if (m_debug) qDebug() << "shouldShowTask: rejected skipSwitcher" << data.title;
        return false;
    }
    if (KoolDockSettings::ignoreList().contains(data.title)) {
        if (m_debug) qDebug() << "shouldShowTask: rejected ignoreList" << data.title;
        return false;
    }
    if (KoolDockSettings::minimizedOnly() && !data.minimized) {
        if (m_debug) qDebug() << "shouldShowTask: rejected minimizedOnly" << data.title;
        return false;
    }
    if (KoolDockSettings::currentDesktopOnly() && KWindowSystem::isPlatformX11() &&
        !data.onAllDesktops && data.desktop != KX11Extras::currentDesktop()) {
        if (m_debug) qDebug() << "shouldShowTask: rejected currentDesktopOnly" << data.title;
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
    if (m_debug) qDebug() << "DockModel::onWindowAdded id=" << windowId;
    if (!KoolDockSettings::showTaskbar() || !m_tasks || !m_tasks->isAvailable()) {
        if (m_debug) qDebug() << "  rejected: showTaskbar=" << KoolDockSettings::showTaskbar()
                 << "tasks=" << m_tasks << "available=" << (m_tasks ? m_tasks->isAvailable() : false);
        return;
    }
    if (m_taskItems.contains(windowId)) {
        if (m_debug) qDebug() << "  already tracked";
        return;
    }

    const WindowTasks::TaskData data = m_tasks->taskData(windowId);
    if (m_debug) qDebug() << "  title=" << data.title << "icon=" << data.iconName
             << "appId=" << data.appId
             << "skipTaskbar=" << data.skipTaskbar << "skipSwitcher=" << data.skipSwitcher
             << "minimized=" << data.minimized << "active=" << data.active;
    if (!shouldShowTask(data)) return;

    // Fuse with a matching launcher if one exists.
    // If the launcher already has windows, group the new window under it
    // (KDE Plasma taskbar-style grouping). If it's not yet running, fuse
    // the first window as before.
    Item *launcher = findLauncherForAppId(data.appId);
    if (m_debug && !launcher) {
        qDebug() << "  no matching launcher found for appId=" << data.appId;
        qDebug() << "  existing launchers:";
        for (Item *item : m_items) {
            if (item->isLauncher() && !item->desktopFile().isEmpty()) {
                KDesktopFile df(item->desktopFile());
                const QString exec = df.desktopGroup().readEntry(QStringLiteral("Exec"), QString());
                qDebug() << "   " << item->name() << "desktopFile=" << item->desktopFile() << "exec=" << exec;
            }
        }
    }
    if (launcher) {
        if (!launcher->isRunning()) {
            if (m_debug) qDebug() << "  fused with launcher" << launcher->name() << "windowCount=" << launcher->windowCount();
            launcher->setRunning(true);
            launcher->setWindowId(windowId);
            launcher->setActive(data.active);
            launcher->setMinimized(data.minimized);
            m_taskItems.insert(windowId, launcher);
            const int row = m_items.indexOf(launcher);
            if (row >= 0) Q_EMIT dataChanged(index(row), index(row));
            if (!m_loading) {
                if (row >= 0) Q_EMIT itemChanged(row);
                Q_EMIT countChanged();
                Q_EMIT itemsChanged();
            }
            return;
        }
        // Group additional windows under the same launcher
        if (m_debug) qDebug() << "  grouping window under launcher" << launcher->name() << "windowCount before=" << launcher->windowCount();
        launcher->addGroupedWindowId(windowId);
        launcher->setActive(data.active);
        launcher->setMinimized(data.minimized);
        m_taskItems.insert(windowId, launcher);
        const int row = m_items.indexOf(launcher);
        if (row >= 0) Q_EMIT dataChanged(index(row), index(row));
        if (!m_loading) {
            if (row >= 0) Q_EMIT itemChanged(row);
            Q_EMIT itemsChanged();
        }
        if (m_debug) qDebug() << "  windowCount after=" << launcher->windowCount();
        return;
    }

    // No matching launcher: check if there's already a standalone task for
    // this app to group under.
    Item *existingTask = findTaskForAppId(data.appId);
    if (existingTask) {
        if (m_debug) qDebug() << "  grouping window under task" << existingTask->name() << "appId=" << existingTask->appId() << "windowCount before=" << existingTask->windowCount();
        existingTask->addGroupedWindowId(windowId);
        existingTask->setActive(data.active);
        existingTask->setMinimized(data.minimized);
        existingTask->setName(data.title);
        m_taskItems.insert(windowId, existingTask);
        const int row = m_items.indexOf(existingTask);
        if (row >= 0) Q_EMIT dataChanged(index(row), index(row));
        if (!m_loading) {
            if (row >= 0) Q_EMIT itemChanged(row);
            Q_EMIT itemsChanged();
        }
        if (m_debug) qDebug() << "  windowCount after=" << existingTask->windowCount();
        return;
    }

    if (m_debug) qDebug() << "  creating new standalone task for" << data.title;
    const QString iconName = resolveIconName(data.appId, data.iconName);
    auto *item = new Item(Item::Kind::Task, data.title, iconName, QString(), windowId);
    item->setMinimized(data.minimized);
    item->setActive(data.active);
    item->setAppId(data.appId);
    m_taskItems.insert(windowId, item);
    insertTaskSorted(item);
    qDebug() << "  inserted, count=" << m_items.size();
}

void DockModel::onWindowRemoved(quint64 windowId)
{
    if (!m_taskItems.contains(windowId)) return;
    Item *item = m_taskItems.take(windowId);
    if (m_debug) qDebug() << "DockModel::onWindowRemoved id=" << windowId << "item=" << (item ? item->name() : QStringLiteral("null")) << "isLauncher=" << (item ? item->isLauncher() : false) << "windowCount=" << (item ? item->windowCount() : 0);

    if (item->isLauncher()) {
        // Fused launcher: remove window from group
        item->removeGroupedWindowId(windowId);
        if (m_debug) qDebug() << "  launcher windowCount after remove=" << item->windowCount();
        if (item->windowCount() == 0) {
            if (m_debug) qDebug() << "  last window removed, unfusing launcher";
            item->setRunning(false);
            item->setActive(false);
            item->setMinimized(false);
        } else {
            const WindowTasks::TaskData data = m_tasks->taskData(item->windowId());
            item->setActive(data.active);
            item->setMinimized(data.minimized);
        }
        const int row = m_items.indexOf(item);
        if (row >= 0) {
            Q_EMIT dataChanged(index(row), index(row));
            Q_EMIT itemChanged(row);
        }
        Q_EMIT countChanged();
        Q_EMIT itemsChanged();
        return;
    }

    // Standalone task: if it has other grouped windows, keep the item
    if (item->windowCount() > 1) {
        if (m_debug) qDebug() << "  task has more windows, keeping item";
        item->removeGroupedWindowId(windowId);
        const WindowTasks::TaskData data = m_tasks->taskData(item->windowId());
        item->setActive(data.active);
        item->setMinimized(data.minimized);
        item->setName(data.title);
        const int row = m_items.indexOf(item);
        if (row >= 0) {
            Q_EMIT dataChanged(index(row), index(row));
            Q_EMIT itemChanged(row);
        }
        Q_EMIT itemsChanged();
        return;
    }

    // Last window of standalone task: remove the item
    if (m_debug) qDebug() << "  removing standalone task item";
    const int row = m_items.indexOf(item);
    if (row >= 0) {
        beginRemoveRows({}, row, row);
        m_items.removeAt(row);
        endRemoveRows();
        updateIndices();
        Q_EMIT itemRemoved(row);
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
    if (m_debug) qDebug() << "DockModel::onWindowChanged id=" << windowId << "title=" << data.title << "appId=" << data.appId << "item appId=" << item->appId();

    if (!shouldShowTask(data)) {
        onWindowRemoved(windowId);
        return;
    }

    // When a standalone task gets its appId filled in for the first time
    // (Wayland delivers app_id asynchronously after window creation), check
    // if there's a matching launcher or existing task to group with.
    if (item->isTask() && item->appId().isEmpty() && !data.appId.isEmpty()) {
        item->setAppId(data.appId);

        // Try to find a launcher to fuse with
        Item *launcher = findLauncherForAppId(data.appId);
        if (launcher) {
            if (m_debug) qDebug() << "  retro-fusing with launcher" << launcher->name();
            // Remove the standalone task and move window to launcher
            m_taskItems.remove(windowId);
            m_taskItems.insert(windowId, launcher);
            if (!launcher->isRunning()) {
                launcher->setRunning(true);
                launcher->setWindowId(windowId);
            } else {
                launcher->addGroupedWindowId(windowId);
            }
            launcher->setActive(data.active);
            launcher->setMinimized(data.minimized);
            item->setAppId(QString());
            const int oldRow = m_items.indexOf(item);
            if (oldRow >= 0) {
                beginRemoveRows({}, oldRow, oldRow);
                m_items.removeAt(oldRow);
                endRemoveRows();
                updateIndices();
                Q_EMIT itemRemoved(oldRow);
                Q_EMIT countChanged();
            }
            delete item;
            const int newRow = m_items.indexOf(launcher);
            if (newRow >= 0) {
                Q_EMIT dataChanged(index(newRow), index(newRow));
                Q_EMIT itemChanged(newRow);
            }
            Q_EMIT itemsChanged();
            return;
        }

        // Try to find an existing standalone task to group with
        Item *existingTask = findTaskForAppId(data.appId);
        if (existingTask && existingTask != item) {
            if (m_debug) qDebug() << "  retro-grouping with existing task" << existingTask->name();
            // Remove this standalone task and move window to the existing task
            m_taskItems.remove(windowId);
            m_taskItems.insert(windowId, existingTask);
            existingTask->addGroupedWindowId(windowId);
            existingTask->setActive(data.active);
            existingTask->setMinimized(data.minimized);
            existingTask->setName(data.title);
            item->setAppId(QString());
            const int oldRow = m_items.indexOf(item);
            if (oldRow >= 0) {
                beginRemoveRows({}, oldRow, oldRow);
                m_items.removeAt(oldRow);
                endRemoveRows();
                updateIndices();
                Q_EMIT itemRemoved(oldRow);
                Q_EMIT countChanged();
            }
            delete item;
            const int newRow = m_items.indexOf(existingTask);
            if (newRow >= 0) {
                Q_EMIT dataChanged(index(newRow), index(newRow));
                Q_EMIT itemChanged(newRow);
            }
            Q_EMIT itemsChanged();
            return;
        }
    }

    // Update fused launcher or standalone task.
    item->setActive(data.active);
    item->setMinimized(data.minimized);
    if (item->isTask()) {
        item->setName(data.title);
        item->setIconName(resolveIconName(data.appId, data.iconName));
        item->setAppId(data.appId);
    }

    const int row = m_items.indexOf(item);
    if (row >= 0) {
        Q_EMIT dataChanged(index(row), index(row));
        Q_EMIT itemChanged(row);
        Q_EMIT itemsChanged();
    }
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

void DockModel::onLaunchersChanged()
{
    if (m_suppressReload) return;
    reload();
}

void DockModel::rebuild() { reload(); }

void DockModel::addLauncher(const QString &filePath)
{
    m_launchers->addLauncher(filePath);
}

void DockModel::removeLauncher(int row)
{
    if (row < 0 || row >= m_items.size()) return;
    Item *item = m_items.at(row);
    if (!item->isLauncher()) return;
    // Count only launchers up to this row to get the launcher index.
    int launcherIdx = 0;
    for (int i = 0; i < row; i++) {
        if (m_items.at(i)->isLauncher()) launcherIdx++;
    }
    // Remove from the on-disk launcher store without triggering a full
    // model reload — we'll remove the row ourselves below.
    m_suppressReload = true;
    m_launchers->removeLauncher(launcherIdx);
    m_suppressReload = false;

    // If this launcher was fused with a window, un-fuse it so the task
    // doesn't try to reference the deleted item.
    if (item->windowId() && m_taskItems.contains(item->windowId())) {
        m_taskItems.remove(item->windowId());
    }

    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    delete item;
    updateIndices();
    Q_EMIT itemRemoved(row);
    Q_EMIT countChanged();
    Q_EMIT itemsChanged();
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
    m_suppressReload = true;
    m_launchers->moveLauncher(fromLauncher, toLauncher);
    m_suppressReload = false;

    // Move the item within m_items.
    if (from == to) return;
    // beginMoveRows works with the model before modification.
    // Subtract one from to when moving forward because the item's row
    // shifts after removal, but we emit the move against the *current*
    // model state.
    const int destRow = (to > from) ? to + 1 : to;
    if (!beginMoveRows({}, from, from, {}, destRow)) return;
    m_items.move(from, to);
    endMoveRows();
    updateIndices();
    Q_EMIT itemMoved(from, to);
    Q_EMIT countChanged();
    Q_EMIT itemsChanged();
}

bool DockModel::isLauncher(int row) const
{
    if (row < 0 || row >= m_items.size()) return false;
    return m_items.at(row)->isLauncher();
}

QString DockModel::desktopPathForAppId(const QString &appId) const
{
    if (appId.isEmpty()) return {};

    auto tryService = [](const QString &name) -> QString {
        const KService::Ptr service = KService::serviceByDesktopName(name);
        if (service) {
            const QString path = service->entryPath();
            if (!path.isEmpty() && QFile::exists(path)) return path;
        }
        return {};
    };

    // 1. Exact match as provided by the compositor.
    QString path = tryService(appId);
    if (!path.isEmpty()) return path;

    // 2. Strip "org.kde." prefix (common for KDE apps).
    if (appId.startsWith(QStringLiteral("org.kde."))) {
        path = tryService(appId.mid(8));
        if (!path.isEmpty()) return path;
    }

    // 3. If the appId looks like reverse-DNS (contains dots), try each
    // dot-separated component as a desktop name.  Many apps use IDs like
    // "com.github.user.AppName" where the desktop file is "AppName.desktop"
    // or "com.github.user.AppName.desktop".
    if (appId.contains(QLatin1Char('.'))) {
        const QStringList parts = appId.split(QLatin1Char('.'));
        // Try the last component first (most likely to be the app name).
        if (!parts.last().isEmpty()) {
            path = tryService(parts.last());
            if (!path.isEmpty()) return path;
            // Try case-insensitive.
            path = tryService(parts.last().toLower());
            if (!path.isEmpty()) return path;
        }
        // Try the second-to-last component (e.g. "net.sourceforge.MultiVNC"
        // where "sourceforge" is not the app name but "MultiVNC" is last).
        if (parts.size() >= 2 && !parts.at(parts.size() - 2).isEmpty()) {
            path = tryService(parts.at(parts.size() - 2));
            if (!path.isEmpty()) return path;
        }
    }

    // 4. Case-insensitive exact match.
    const QString lower = appId.toLower();
    if (lower != appId) {
        path = tryService(lower);
        if (!path.isEmpty()) return path;
    }

    // 5. Search well-known application directories for desktop files
    // whose base name (without .desktop) fuzzy-matches the appId.
    const QStringList dirs = {
        QStringLiteral("/usr/share/applications"),
        QStringLiteral("/usr/local/share/applications"),
        QStringLiteral("/var/lib/flatpak/exports/share/applications"),
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/applications"),
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/flatpak/exports/share/applications"),
    };
    for (const QString &dir : dirs) {
        QDir d(dir);
        if (!d.exists()) continue;
        const QStringList files = d.entryList({QStringLiteral("*.desktop")}, QDir::Files);
        // Exact match first.
        for (const QString &file : files) {
            const QString name = file.chopped(8);
            if (name.compare(appId, Qt::CaseInsensitive) == 0)
                return d.absoluteFilePath(file);
        }
        // Partial match: the appId is often the last component of a
        // reverse-DNS Flatpak ID (e.g. appId="multivnc" matches
        // "net.christianbeier.MultiVNC.desktop").
        for (const QString &file : files) {
            const QString name = file.chopped(8);
            if (name.contains(appId, Qt::CaseInsensitive))
                return d.absoluteFilePath(file);
        }
    }

    return {};
}

QString DockModel::resolveIconName(const QString &appId, const QString &waylandIconName) const
{
    // Prefer the .desktop file's Icon= as the canonical source — this is
    // what the KDE taskbar does via libtaskmanager (KService → readIcon()).
    // The Wayland protocol's themed_icon_name is only a fallback for apps
    // that don't have an installed .desktop file (custom/proprietary apps
    // that set their icon at runtime via xdg_toplevel).
    if (!appId.isEmpty()) {
        const QString desktopPath = desktopPathForAppId(appId);
        if (!desktopPath.isEmpty()) {
            KDesktopFile df(desktopPath);
            const QString desktopIcon = df.readIcon();
            if (!desktopIcon.isEmpty())
                return desktopIcon;
        }
    }

    // No .desktop file or its Icon= was empty — use the compositor-provided
    // themed icon name if it resolves in the current icon theme.
    if (!waylandIconName.isEmpty()) {
        if (QIcon::hasThemeIcon(waylandIconName))
            return waylandIconName;
        const QString lower = waylandIconName.toLower();
        if (lower != waylandIconName && QIcon::hasThemeIcon(lower))
            return lower;
    }

    // Last resort: the appId itself might work as an icon name (e.g. for
    // Flatpak apps where the appId is a reverse-DNS name that matches an
    // icon shipped with the Flatpak).
    if (!appId.isEmpty() && QIcon::hasThemeIcon(appId))
        return appId;

    return waylandIconName; // keep whatever we had (likely empty)
}

QString DockModel::desktopFileForRow(int row) const
{
    if (row < 0 || row >= m_items.size()) return {};
    Item *item = m_items.at(row);
    if (!item->desktopFile().isEmpty()) return item->desktopFile();
    // Standalone tasks (not pinned) don't carry a desktopFile — resolve
    // one on the fly from the appId, same as pinTask()'s lookup, just
    // without persisting it onto the item.
    if (item->isTask()) return desktopPathForAppId(item->appId());
    return {};
}

QVariantList DockModel::desktopActions(int row) const
{
    const QString desktopFile = desktopFileForRow(row);
    if (desktopFile.isEmpty()) return {};

    KDesktopFile df(desktopFile);
    QVariantList result;
    for (const QString &actionId : df.readActions()) {
        const KConfigGroup group = df.actionGroup(actionId);
        if (!group.isValid() || group.readEntry(QStringLiteral("Exec"), QString()).isEmpty()) continue;
        result.append(QVariantMap{
            {QStringLiteral("id"), actionId},
            {QStringLiteral("name"), group.readEntry(QStringLiteral("Name"), actionId)},
            {QStringLiteral("iconName"), group.readEntry(QStringLiteral("Icon"), QString())},
        });
    }
    return result;
}

void DockModel::triggerDesktopAction(int row, const QString &actionId)
{
    const QString desktopFile = desktopFileForRow(row);
    if (desktopFile.isEmpty()) return;

    KDesktopFile df(desktopFile);
    const KConfigGroup group = df.actionGroup(actionId);
    const QString exec = group.readEntry(QStringLiteral("Exec"), QString());
    if (exec.isEmpty()) return;

    auto *job = new KIO::CommandLauncherJob(stripExecFieldCodes(exec));
    job->setUiDelegate(nullptr);
    job->start();
}

void DockModel::pinTask(quint64 windowId)
{
    // "Keep in Dock": resolve the running task's appId to a .desktop file
    // and add it as a permanent launcher. The task will fuse with the new
    // launcher on the next reload.
    if (!m_tasks || !windowId) return;
    const WindowTasks::TaskData data = m_tasks->taskData(windowId);
    if (data.appId.isEmpty()) return;

    const QString desktopPath = desktopPathForAppId(data.appId);
    if (desktopPath.isEmpty()) {
        qDebug() << "pinTask: no .desktop found for appId" << data.appId;
        return;
    }
    qDebug() << "pinTask: adding launcher from" << desktopPath << "for appId" << data.appId;
    m_launchers->addLauncher(desktopPath);
    // addLauncher emits changed() which triggers reload().
}

Item *DockModel::findLauncherForAppId(const QString &appId) const
{
    if (appId.isEmpty()) return nullptr;

    auto execBase = [](const QString &exec) -> QString {
        if (exec.isEmpty()) return {};
        QString s = exec;
        s.remove(QRegularExpression(QStringLiteral("%[a-zA-Z]")));
        const QString first = s.section(QLatin1Char(' '), 0, 0);
        const QString firstName = QFileInfo(first).fileName();
        // Flatpak-wrapped apps all launch via "flatpak run ... --command=<bin>
        // ...", so the literal first token is always "flatpak" — every
        // Flatpak app would otherwise collapse to the same identifier and
        // falsely match each other (e.g. Telegram fusing with LibreWolf's
        // launcher). Pull out the real command instead.
        if (firstName == QStringLiteral("flatpak")) {
            const QRegularExpression cmdRe(QStringLiteral("--command=(\\S+)"));
            const auto match = cmdRe.match(s);
            if (match.hasMatch()) return match.captured(1);
        }
        return firstName;
    };

    // Resolve the appId to the installed .desktop service so we can get
    // the actual executable. KService::serviceByDesktopName knows how to
    // map Wayland app_ids (e.g. "org.kde.konsole") to their .desktop files.
    const KService::Ptr taskService = KService::serviceByDesktopName(appId);
    const QString taskProgram = taskService ? execBase(taskService->exec()) : appId;
    if (m_debug) qDebug() << "  findLauncherForAppId: appId=" << appId << "taskService=" << (taskService ? taskService->entryPath() : QStringLiteral("none")) << "taskProgram=" << taskProgram;

    for (Item *item : m_items) {
        if (!item->isLauncher() || item->desktopFile().isEmpty()) continue;
        KDesktopFile df(item->desktopFile());
        const QString launcherExec = df.desktopGroup().readEntry(QStringLiteral("Exec"), QString());
        const QString launcherProgram = execBase(launcherExec);
        if (launcherProgram.isEmpty()) continue;

        if (m_debug) qDebug() << "    checking launcher" << item->name() << "launcherProgram=" << launcherProgram << "taskProgram=" << taskProgram << "appId=" << appId;

        // Match by executable (case-insensitive) — covers both the
        // KService-resolved program and a direct appId comparison for
        // apps where KService didn't find a service (e.g. "librewolf").
        if (launcherProgram.compare(taskProgram, Qt::CaseInsensitive) == 0 ||
            launcherProgram.compare(appId, Qt::CaseInsensitive) == 0) {
            return item;
        }
    }
    return nullptr;
}

Item *DockModel::findTaskForAppId(const QString &appId) const
{
    if (appId.isEmpty()) return nullptr;
    for (Item *item : m_items) {
        if (item->isTask() && item->appId() == appId) {
            if (m_debug) qDebug() << "  findTaskForAppId: found" << item->name() << "for appId=" << appId;
            return item;
        }
    }
    if (m_debug) qDebug() << "  findTaskForAppId: no task found for appId=" << appId;
    return nullptr;
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
        {QStringLiteral("isLauncher"), item->isLauncher()},
        {QStringLiteral("isAppMenu"), item->isAppMenu()},
        {QStringLiteral("isTrash"), item->isTrash()},
        {QStringLiteral("isRunning"), item->isRunning()},
        {QStringLiteral("windowId"), item->windowId()},
        {QStringLiteral("windowCount"), item->windowCount()},
        {QStringLiteral("itemIndex"), item->itemIndex()},
        {QStringLiteral("badgeCount"), item->badgeCount()},
    };
}

QVariantList DockModel::windowListForRow(int row) const
{
    if (row < 0 || row >= m_items.size()) return {};
    Item *item = m_items.at(row);
    if (item->windowCount() <= 1) return {};

    QVariantList list;
    const QList<quint64> ids = item->groupedWindowIds();
    for (quint64 wid : ids) {
        const WindowTasks::TaskData data = m_tasks->taskData(wid);
        QVariantMap entry;
        entry[QStringLiteral("windowId")] = QVariant::fromValue(wid);
        entry[QStringLiteral("title")] = data.title;
        entry[QStringLiteral("active")] = data.active;
        entry[QStringLiteral("minimized")] = data.minimized;
        list.append(entry);
    }
    return list;
}

void DockModel::onBadgeChanged(const QString &desktopId, int count, bool visible)
{
    const int badge = visible ? count : 0;

    // Try a pinned/fused launcher first (reuses the same fuzzy appId<->Exec
    // matching used to fuse running windows with their launcher).
    Item *target = findLauncherForAppId(desktopId);
    if (!target) {
        // Fall back to a standalone task matching by its own appId.
        for (Item *it : m_items) {
            if (it->isTask() && it->appId().compare(desktopId, Qt::CaseInsensitive) == 0) {
                target = it;
                break;
            }
        }
    }
    if (!target || target->badgeCount() == badge) return;

    target->setBadgeCount(badge);
    const int row = m_items.indexOf(target);
    if (row >= 0) {
        Q_EMIT itemChanged(row);
        Q_EMIT itemsChanged();
    }
}

QHash<int, QByteArray> DockModel::roleNames() const
{
    return {{Qt::DisplayRole, "display"}, {Qt::DecorationRole, "decoration"}};
}

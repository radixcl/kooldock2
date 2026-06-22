// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "waylandwindowtasks.h"

#include <QDebug>
#include <QGuiApplication>
#include <qnativeinterface.h>

#include <cstring>
#include <wayland-client.h>

PlasmaWindow::PlasmaWindow(quint64 id, struct ::org_kde_plasma_window *object, QObject *parent)
    : QObject(parent)
{
    m_info.windowId = id;
    init(object);
}

PlasmaWindow::~PlasmaWindow()
{
    if (object()) {
        destroy();
    }
}

void PlasmaWindow::requestActivate()
{
    if (!object()) {
        return;
    }
    set_state(QtWayland::org_kde_plasma_window_management::state_active,
              QtWayland::org_kde_plasma_window_management::state_active);
}

void PlasmaWindow::requestMinimize()
{
    if (!object()) {
        return;
    }
    set_state(QtWayland::org_kde_plasma_window_management::state_minimized,
              QtWayland::org_kde_plasma_window_management::state_minimized);
}

void PlasmaWindow::requestClose()
{
    if (!object()) {
        return;
    }
    close();
}

void PlasmaWindow::org_kde_plasma_window_title_changed(const QString &title)
{
    if (m_info.title == title) {
        return;
    }
    m_info.title = title;
    Q_EMIT infoChanged();
}

void PlasmaWindow::org_kde_plasma_window_app_id_changed(const QString &app_id)
{
    if (m_info.appId == app_id) {
        return;
    }
    m_info.appId = app_id;
    Q_EMIT infoChanged();
}

void PlasmaWindow::org_kde_plasma_window_state_changed(uint32_t flags)
{
    if (m_info.state == flags) {
        return;
    }
    m_info.state = flags;
    Q_EMIT infoChanged();
}

void PlasmaWindow::org_kde_plasma_window_themed_icon_name_changed(const QString &name)
{
    if (m_info.iconName == name) {
        return;
    }
    m_info.iconName = name;
    Q_EMIT infoChanged();
}

void PlasmaWindow::org_kde_plasma_window_unmapped()
{
    Q_EMIT unmapped();
}

PlasmaWindowManagement::PlasmaWindowManagement(QObject *parent)
    : QObject(parent)
{
    qDebug() << "PlasmaWindowManagement created";
}

PlasmaWindowManagement::~PlasmaWindowManagement()
{
    qDeleteAll(m_windows);
}

void PlasmaWindowManagement::bind(struct ::wl_registry *registry, uint32_t id, uint32_t version)
{
    const uint32_t bindVersion = qMin(version, 20u);
    qDebug() << "PlasmaWindowManagement binding id=" << id << "version=" << bindVersion;
    init(registry, id, bindVersion);
}

WindowTaskInfo PlasmaWindowManagement::windowInfo(quint64 id) const
{
    PlasmaWindow *window = m_windows.value(id);
    return window ? window->info() : WindowTaskInfo{};
}

void PlasmaWindowManagement::org_kde_plasma_window_management_window(uint32_t id)
{
    const quint64 windowId = static_cast<quint64>(id);
    qDebug() << "PlasmaWindowManagement::window id=" << windowId << "existing=" << m_windows.contains(windowId);
    if (m_windows.contains(windowId)) {
        return;
    }

    struct ::org_kde_plasma_window *windowObject = get_window(id);
    if (!windowObject) {
        qWarning() << "PlasmaWindowManagement::window get_window returned null for id=" << windowId;
        return;
    }

    auto *window = new PlasmaWindow(windowId, windowObject, this);
    m_windows.insert(windowId, window);

    connect(window, &PlasmaWindow::infoChanged, this, [this, windowId]() {
        onWindowInfoChanged(windowId);
    });
    connect(window, &PlasmaWindow::unmapped, this, &PlasmaWindowManagement::onWindowUnmapped);

    Q_EMIT windowAdded(windowId);
    updateActiveWindow();
}

void PlasmaWindowManagement::onWindowInfoChanged(quint64 id)
{
    Q_EMIT windowChanged(id);
    updateActiveWindow();
}

void PlasmaWindowManagement::onWindowUnmapped()
{
    auto *window = qobject_cast<PlasmaWindow *>(sender());
    if (!window) {
        return;
    }

    const quint64 id = window->info().windowId;
    if (m_activeWindow == id) {
        m_activeWindow = 0;
        Q_EMIT activeWindowChanged(0);
    }

    m_windows.remove(id);
    window->deleteLater();
    Q_EMIT windowRemoved(id);
}

void PlasmaWindowManagement::updateActiveWindow()
{
    quint64 newActive = 0;
    for (auto it = m_windows.cbegin(); it != m_windows.cend(); ++it) {
        if (it.value()->info().state & QtWayland::org_kde_plasma_window_management::state_active) {
            newActive = it.key();
            break;
        }
    }

    if (m_activeWindow == newActive) {
        return;
    }
    m_activeWindow = newActive;
    Q_EMIT activeWindowChanged(newActive);
}

WaylandWindowTasks::WaylandWindowTasks(QObject *parent)
    : QObject(parent)
{
}

WaylandWindowTasks::~WaylandWindowTasks()
{
    if (m_registry) {
        wl_registry_destroy(m_registry);
    }
}

bool WaylandWindowTasks::isActive() const
{
    return m_management && m_management->isReady();
}

void WaylandWindowTasks::start()
{
    if (m_management || m_registry) {
        return;
    }

    auto *waylandApp = qApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!waylandApp) {
        qWarning() << "WaylandWindowTasks: not a Wayland application";
        return;
    }

    struct wl_display *display = waylandApp->display();
    if (!display) {
        qWarning() << "WaylandWindowTasks: no Wayland display";
        return;
    }

    m_registry = wl_display_get_registry(display);
    if (!m_registry) {
        qWarning() << "WaylandWindowTasks: failed to get wl_registry";
        return;
    }

    static const struct wl_registry_listener registryListener = {
        .global = [](void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
            Q_UNUSED(registry)
            auto *self = static_cast<WaylandWindowTasks *>(data);
            self->handleRegistryGlobal(registry, id, interface, version);
        },
        .global_remove = [](void *data, struct wl_registry *registry, uint32_t id) {
            Q_UNUSED(data)
            Q_UNUSED(registry)
            Q_UNUSED(id)
        }
    };

    wl_registry_add_listener(m_registry, &registryListener, this);
    wl_display_roundtrip(display);

    if (!m_management) {
        qWarning() << "WaylandWindowTasks: org_kde_plasma_window_management not advertised by compositor";
    }
}

void WaylandWindowTasks::handleRegistryGlobal(struct ::wl_registry *registry, uint32_t id, const char *interface, uint32_t version)
{
    if (std::strcmp(interface, "org_kde_plasma_window_management") != 0) {
        return;
    }
    if (m_management) {
        return;
    }

    qDebug() << "WaylandWindowTasks: found org_kde_plasma_window_management id=" << id << "version=" << version;
    m_management = new PlasmaWindowManagement(this);
    connect(m_management, &PlasmaWindowManagement::windowAdded, this, &WaylandWindowTasks::windowAdded);
    connect(m_management, &PlasmaWindowManagement::windowRemoved, this, &WaylandWindowTasks::windowRemoved);
    connect(m_management, &PlasmaWindowManagement::windowChanged, this, &WaylandWindowTasks::windowChanged);
    connect(m_management, &PlasmaWindowManagement::activeWindowChanged, this, [this](quint64 windowId) {
        m_activeWindow = windowId;
        Q_EMIT activeWindowChanged(windowId);
    });
    m_management->bind(registry, id, version);
    Q_EMIT activeChanged();
}

QList<quint64> WaylandWindowTasks::windowIds() const
{
    return m_management ? m_management->windows().keys() : QList<quint64>{};
}

quint64 WaylandWindowTasks::activeWindow() const
{
    return m_activeWindow;
}

WaylandWindowTasks::TaskData WaylandWindowTasks::taskData(quint64 windowId) const
{
    TaskData data;
    if (!m_management) {
        return data;
    }

    const WindowTaskInfo info = m_management->windowInfo(windowId);
    data.windowId = info.windowId;
    data.title = info.title;
    data.iconName = info.iconName;
    data.minimized = info.state & QtWayland::org_kde_plasma_window_management::state_minimized;
    data.active = info.state & QtWayland::org_kde_plasma_window_management::state_active;
    data.skipTaskbar = info.state & QtWayland::org_kde_plasma_window_management::state_skiptaskbar;
    data.skipSwitcher = info.state & QtWayland::org_kde_plasma_window_management::state_skipswitcher;
    data.onAllDesktops = info.state & QtWayland::org_kde_plasma_window_management::state_on_all_desktops;
    return data;
}

void WaylandWindowTasks::requestActivate(quint64 windowId)
{
    if (!m_management) {
        return;
    }
    PlasmaWindow *window = m_management->windows().value(windowId);
    if (window) {
        window->requestActivate();
    }
}

void WaylandWindowTasks::requestMinimize(quint64 windowId)
{
    if (!m_management) {
        return;
    }
    PlasmaWindow *window = m_management->windows().value(windowId);
    if (window) {
        window->requestMinimize();
    }
}

void WaylandWindowTasks::requestClose(quint64 windowId)
{
    if (!m_management) {
        return;
    }
    PlasmaWindow *window = m_management->windows().value(windowId);
    if (window) {
        window->requestClose();
    }
}

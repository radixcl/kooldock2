// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "waylandwindowtasks.h"

#include <QDebug>
#include <QGuiApplication>
#include <QIcon>
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

void PlasmaWindow::requestToggleState(uint32_t bit)
{
    if (!object()) {
        return;
    }
    const bool isSet = m_info.state & bit;
    set_state(bit, isSet ? 0 : bit);
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

quint64 PlasmaWindowManagement::ensureWindowId(const QString &uuid)
{
    auto it = m_uuidToId.constFind(uuid);
    if (it != m_uuidToId.constEnd()) {
        return it.value();
    }
    const quint64 id = ++m_nextWindowId;
    m_uuidToId.insert(uuid, id);
    return id;
}

quint64 PlasmaWindowManagement::addWindow(const QString &uuid)
{
    const quint64 windowId = ensureWindowId(uuid);
    qDebug() << "PlasmaWindowManagement::addWindow uuid=" << uuid << "windowId=" << windowId
             << "existing=" << m_windows.contains(windowId);
    if (m_windows.contains(windowId)) {
        return windowId;
    }

    struct ::org_kde_plasma_window *windowObject = get_window_by_uuid(uuid);
    if (!windowObject) {
        qWarning() << "PlasmaWindowManagement::addWindow get_window_by_uuid returned null for uuid=" << uuid;
        return 0;
    }

    auto *window = new PlasmaWindow(windowId, windowObject, this);
    m_windows.insert(windowId, window);

    connect(window, &PlasmaWindow::infoChanged, this, [this, windowId]() {
        onWindowInfoChanged(windowId);
    });
    connect(window, &PlasmaWindow::unmapped, this, &PlasmaWindowManagement::onWindowUnmapped);

    Q_EMIT windowAdded(windowId);
    updateActiveWindow();
    return windowId;
}

WindowTaskInfo PlasmaWindowManagement::windowInfo(quint64 id) const
{
    PlasmaWindow *window = m_windows.value(id);
    return window ? window->info() : WindowTaskInfo{};
}

void PlasmaWindowManagement::org_kde_plasma_window_management_window(uint32_t id)
{
    // Legacy event (protocol < 13): only carries a numeric id, no uuid.
    // We can't use get_window_by_uuid here, so fall back to get_window.
    // On modern KWin (protocol 20) this event is not sent.
    const quint64 windowId = static_cast<quint64>(id);
    qDebug() << "PlasmaWindowManagement::window (legacy) id=" << windowId
             << "existing=" << m_windows.contains(windowId);
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

void PlasmaWindowManagement::org_kde_plasma_window_management_window_with_uuid(uint32_t id, const QString &uuid)
{
    // Modern event (protocol >= 13): carries both a deprecated numeric id
    // and a uuid. We use the uuid with get_window_by_uuid to create the
    // window object. This is what KWin (protocol 20) sends for newly
    // mapped windows after bind.
    Q_UNUSED(id);
    qDebug() << "PlasmaWindowManagement::window_with_uuid uuid=" << uuid;
    addWindow(uuid);
}

void PlasmaWindowManagement::org_kde_plasma_window_management_stacking_order_changed_2()
{
    // Sent on bind and when stacking order changes (protocol >= 17).
    // Request the current stacking order to get all existing windows.
    qDebug() << "PlasmaWindowManagement::stacking_order_changed_2";
    struct ::org_kde_plasma_stacking_order *so = get_stacking_order();
    if (so) {
        new PlasmaStackingOrder(so, this);
    }
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

PlasmaStackingOrder::PlasmaStackingOrder(struct ::org_kde_plasma_stacking_order *object, PlasmaWindowManagement *management)
    : QObject(management)
    , m_management(management)
{
    init(object);
}

PlasmaStackingOrder::~PlasmaStackingOrder()
{
    // The compositor destroys the wl object after sending `done()`. No
    // client-side destroy() request exists for this interface — the
    // generated binding has no destroy() method (done is a destructor
    // event, not a destructor request).
}

void PlasmaStackingOrder::org_kde_plasma_stacking_order_window(const QString &uuid)
{
    if (m_management) {
        m_management->addWindow(uuid);
    }
}

void PlasmaStackingOrder::org_kde_plasma_stacking_order_done()
{
    deleteLater();
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
    // Roundtrip 1: fires the registry's `global` callback, which binds to
    // org_kde_plasma_window_management. After bind, the compositor queues
    // the `stacking_order_changed_2` event (and the initial window events
    // once we request the stacking order).
    wl_display_roundtrip(display);

    if (m_management) {
        // Roundtrip 2: dispatches `stacking_order_changed_2` (which calls
        // get_stacking_order in its handler), and the stacking order's
        // `window(uuid)` events that create the initial PlasmaWindow
        // objects via get_window_by_uuid. The compositor queues each
        // window's initial state events (title, app_id, state, etc.).
        wl_display_roundtrip(display);
        // Roundtrip 3: dispatches the initial window state events so the
        // dock has title/icon/state for each window before it renders.
        wl_display_roundtrip(display);
    }

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
    data.appId = info.appId;
    // Prefer themed_icon_name when it resolves to a real theme icon.
    // Fall back to appId when themed_icon_name is empty OR when it names
    // something that isn't in the icon theme (e.g. some apps report
    // "wayland" as the themed icon name, which isn't a real icon).
    // The appId is typically the desktop file name (e.g. "firefox",
    // "librewolf") which QIcon::fromTheme can resolve.
    if (!info.iconName.isEmpty() && QIcon::hasThemeIcon(info.iconName)) {
        data.iconName = info.iconName;
    } else if (!info.appId.isEmpty() && QIcon::hasThemeIcon(info.appId)) {
        data.iconName = info.appId;
    } else {
        data.iconName = info.iconName.isEmpty() ? info.appId : info.iconName;
    }
    data.minimized = info.state & QtWayland::org_kde_plasma_window_management::state_minimized;
    data.active = info.state & QtWayland::org_kde_plasma_window_management::state_active;
    data.skipTaskbar = info.state & QtWayland::org_kde_plasma_window_management::state_skiptaskbar;
    data.skipSwitcher = info.state & QtWayland::org_kde_plasma_window_management::state_skipswitcher;
    data.onAllDesktops = info.state & QtWayland::org_kde_plasma_window_management::state_on_all_desktops;
    data.maximized = info.state & QtWayland::org_kde_plasma_window_management::state_maximized;
    data.keepAbove = info.state & QtWayland::org_kde_plasma_window_management::state_keep_above;
    data.keepBelow = info.state & QtWayland::org_kde_plasma_window_management::state_keep_below;
    data.fullscreen = info.state & QtWayland::org_kde_plasma_window_management::state_fullscreen;
    data.shaded = info.state & QtWayland::org_kde_plasma_window_management::state_shaded;
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

void WaylandWindowTasks::requestToggleState(quint64 windowId, uint32_t bit)
{
    if (!m_management) {
        return;
    }
    PlasmaWindow *window = m_management->windows().value(windowId);
    if (window) {
        window->requestToggleState(bit);
    }
}

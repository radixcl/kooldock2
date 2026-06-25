// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef WAYLANDWINDOWTASKS_H
#define WAYLANDWINDOWTASKS_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include "qwayland-plasma-window-management.h"

struct WindowTaskInfo {
    quint64 windowId = 0;
    QString title;
    QString appId;
    QString iconName;
    uint32_t state = 0;
};

class PlasmaWindow : public QObject, public QtWayland::org_kde_plasma_window
{
    Q_OBJECT
public:
    explicit PlasmaWindow(quint64 id, struct ::org_kde_plasma_window *object, QObject *parent = nullptr);
    ~PlasmaWindow() override;

    WindowTaskInfo info() const { return m_info; }

    void requestActivate();
    void requestMinimize();
    void requestClose();
    void requestToggleState(uint32_t bit);
    void setMinimizedGeometry(struct ::wl_surface *panel, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void unsetMinimizedGeometry(struct ::wl_surface *panel);

Q_SIGNALS:
    void infoChanged();
    void unmapped();

protected:
    void org_kde_plasma_window_title_changed(const QString &title) override;
    void org_kde_plasma_window_app_id_changed(const QString &app_id) override;
    void org_kde_plasma_window_state_changed(uint32_t flags) override;
    void org_kde_plasma_window_themed_icon_name_changed(const QString &name) override;
    void org_kde_plasma_window_unmapped() override;

private:
    WindowTaskInfo m_info;
};

class PlasmaWindowManagement : public QObject, public QtWayland::org_kde_plasma_window_management
{
    Q_OBJECT
public:
    explicit PlasmaWindowManagement(QObject *parent = nullptr);
    ~PlasmaWindowManagement() override;

    void bind(struct ::wl_registry *registry, uint32_t id, uint32_t version);
    bool isReady() const { return object() != nullptr; }

    QHash<quint64, PlasmaWindow *> windows() const { return m_windows; }
    WindowTaskInfo windowInfo(quint64 id) const;

    // Create a PlasmaWindow from a uuid. Called from both the
    // window_with_uuid event (newly mapped windows) and from
    // PlasmaStackingOrder (existing windows at bind time). Returns the
    // numeric windowId, or 0 if the window already exists or creation
    // failed.
    quint64 addWindow(const QString &uuid);

    Q_SIGNALS:
    void windowAdded(quint64 windowId);
    void windowRemoved(quint64 windowId);
    void windowChanged(quint64 windowId);
    void activeWindowChanged(quint64 windowId);

protected:
    // Legacy event (protocol < 13): window announced with a numeric id.
    // Kept for backward compat with older compositors.
    void org_kde_plasma_window_management_window(uint32_t id) override;
    // Modern event (protocol >= 13): window announced with a uuid.
    void org_kde_plasma_window_management_window_with_uuid(uint32_t id, const QString &uuid) override;
    // Sent on bind and when stacking order changes (protocol >= 17).
    // Client should call get_stacking_order() to receive the current
    // window list.
    void org_kde_plasma_window_management_stacking_order_changed_2() override;

private:
    void onWindowInfoChanged(quint64 id);
    void onWindowUnmapped();
    void updateActiveWindow();
    quint64 ensureWindowId(const QString &uuid);

    QHash<quint64, PlasmaWindow *> m_windows;
    QHash<QString, quint64> m_uuidToId;
    quint64 m_nextWindowId = 0;
    quint64 m_activeWindow = 0;
};

// Helper object for receiving the initial window list at bind time
// (protocol >= 17). The compositor sends a `window(uuid)` event for each
// window in the stacking order, then `done()` and destroys the object.
// For each uuid, PlasmaWindowManagement::addWindow() is called to create
// the org_kde_plasma_window object.
class PlasmaStackingOrder : public QObject, public QtWayland::org_kde_plasma_stacking_order
{
    Q_OBJECT
public:
    explicit PlasmaStackingOrder(struct ::org_kde_plasma_stacking_order *object, PlasmaWindowManagement *management);
    ~PlasmaStackingOrder() override;

protected:
    void org_kde_plasma_stacking_order_window(const QString &uuid) override;
    void org_kde_plasma_stacking_order_done() override;

private:
    PlasmaWindowManagement *m_management;
};

class WaylandWindowTasks : public QObject
{
    Q_OBJECT
public:
    explicit WaylandWindowTasks(QObject *parent = nullptr);
    ~WaylandWindowTasks() override;

    bool isActive() const;

    struct TaskData {
        quint64 windowId = 0;
        QString title;
        QString iconName;
        QString appId;
        bool minimized = false;
        bool active = false;
        bool skipTaskbar = false;
        bool skipSwitcher = false;
        bool onAllDesktops = false;
        bool maximized = false;
        bool keepAbove = false;
        bool keepBelow = false;
        bool fullscreen = false;
        bool shaded = false;
    };

    void start();

    void handleRegistryGlobal(struct ::wl_registry *registry, uint32_t id, const char *interface, uint32_t version);

    QList<quint64> windowIds() const;
    quint64 activeWindow() const;
    TaskData taskData(quint64 windowId) const;
    void requestActivate(quint64 windowId);
    void requestMinimize(quint64 windowId);
    void requestClose(quint64 windowId);
    void requestToggleState(quint64 windowId, uint32_t bit);
    PlasmaWindow *window(quint64 windowId) const;

Q_SIGNALS:
    void windowAdded(quint64 windowId);
    void windowRemoved(quint64 windowId);
    void windowChanged(quint64 windowId);
    void activeWindowChanged(quint64 windowId);
    void activeChanged();

private:
    PlasmaWindowManagement *m_management = nullptr;
    struct wl_registry *m_registry = nullptr;
    quint64 m_activeWindow = 0;
};

#endif

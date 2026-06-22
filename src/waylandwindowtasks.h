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

Q_SIGNALS:
    void windowAdded(quint64 windowId);
    void windowRemoved(quint64 windowId);
    void windowChanged(quint64 windowId);
    void activeWindowChanged(quint64 windowId);

protected:
    void org_kde_plasma_window_management_window(uint32_t id) override;

private:
    void onWindowInfoChanged(quint64 id);
    void onWindowUnmapped();
    void updateActiveWindow();

    QHash<quint64, PlasmaWindow *> m_windows;
    quint64 m_activeWindow = 0;
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
        bool minimized = false;
        bool active = false;
        bool skipTaskbar = false;
        bool skipSwitcher = false;
        bool onAllDesktops = false;
    };

    void start();

    void handleRegistryGlobal(struct ::wl_registry *registry, uint32_t id, const char *interface, uint32_t version);

    QList<quint64> windowIds() const;
    quint64 activeWindow() const;
    TaskData taskData(quint64 windowId) const;
    void requestActivate(quint64 windowId);
    void requestMinimize(quint64 windowId);
    void requestClose(quint64 windowId);

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

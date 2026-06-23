// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "windowtasks.h"

#include "waylandwindowtasks.h"

#include <KWindowInfo>
#include <KWindowSystem>
#include <KX11Extras>

#include <QDebug>
#include <QGuiApplication>
#include <qnativeinterface.h>

#include <netwm.h>

static xcb_connection_t *x11Connection()
{
    auto *x11App = qApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11App ? x11App->connection() : nullptr;
}

WindowTasks::WindowTasks(QObject *parent)
    : QObject(parent)
{
    m_isX11 = KWindowSystem::isPlatformX11();
    m_available = m_isX11;
}

bool WindowTasks::isAvailable() const
{
    return m_available;
}

WindowTasks::TaskData WindowTasks::taskData(quint64 windowId) const
{
    TaskData data;
    data.windowId = windowId;

    if (m_waylandTasks) {
        const WaylandWindowTasks::TaskData waylandData = m_waylandTasks->taskData(windowId);
        data.title = waylandData.title;
        data.iconName = waylandData.iconName;
        data.appId = waylandData.appId;
        data.minimized = waylandData.minimized;
        data.active = waylandData.active;
        data.skipTaskbar = waylandData.skipTaskbar;
        data.skipSwitcher = waylandData.skipSwitcher;
        data.onAllDesktops = waylandData.onAllDesktops;
        return data;
    }

    if (!m_isX11) {
        return data;
    }

    const KWindowInfo info(static_cast<WId>(windowId),
                           NET::WMState | NET::WMName | NET::WMVisibleName,
                           NET::WM2WindowClass);
    if (!info.valid()) {
        return data;
    }

    data.title = info.visibleName();
    data.iconName = QString::fromLatin1(info.windowClassClass());
    data.minimized = info.isMinimized();
    data.active = (windowId == m_activeWindow);
    data.skipTaskbar = info.state() & NET::SkipTaskbar;
    data.skipSwitcher = info.state() & NET::SkipSwitcher;
    data.onAllDesktops = (info.desktop() == NET::OnAllDesktops);
    data.desktop = info.desktop();
    return data;
}

void WindowTasks::requestActivate(quint64 windowId)
{
    if (m_waylandTasks) {
        m_waylandTasks->requestActivate(windowId);
        return;
    }
    if (m_isX11) {
        KX11Extras::activateWindow(static_cast<WId>(windowId));
    }
}

void WindowTasks::requestMinimize(quint64 windowId)
{
    if (m_waylandTasks) {
        m_waylandTasks->requestMinimize(windowId);
        return;
    }
    if (m_isX11) {
        KX11Extras::minimizeWindow(static_cast<WId>(windowId));
    }
}

void WindowTasks::requestClose(quint64 windowId)
{
    if (m_waylandTasks) {
        m_waylandTasks->requestClose(windowId);
        return;
    }
    if (m_isX11) {
        NETRootInfo ri(x11Connection(), NET::CloseWindow);
        ri.closeWindowRequest(static_cast<xcb_window_t>(windowId));
    }
}

QList<quint64> WindowTasks::windowIds() const
{
    if (m_waylandTasks) {
        return m_waylandTasks->windowIds();
    }

    QList<quint64> result;
    if (m_isX11 && KX11Extras::self()) {
        const QList<WId> windows = KX11Extras::windows();
        result.reserve(windows.size());
        for (WId wid : windows) {
            result.append(static_cast<quint64>(wid));
        }
    }
    return result;
}

quint64 WindowTasks::activeWindow() const
{
    if (m_waylandTasks) {
        return m_waylandTasks->activeWindow();
    }
    return m_activeWindow;
}

void WindowTasks::start()
{
    qDebug() << "WindowTasks::start() platform=" << QGuiApplication::platformName() << "isX11=" << m_isX11;

    if (m_isX11 && KX11Extras::self()) {
        m_available = true;
        m_activeWindow = static_cast<quint64>(KX11Extras::activeWindow());
        Q_EMIT availabilityChanged();

        connect(KX11Extras::self(), &KX11Extras::windowAdded,
                this, &WindowTasks::slotWindowAdded);
        connect(KX11Extras::self(), &KX11Extras::windowRemoved,
                this, &WindowTasks::slotWindowRemoved);
        connect(KX11Extras::self(), &KX11Extras::windowChanged,
                this, &WindowTasks::slotWindowChanged);
        connect(KX11Extras::self(), &KX11Extras::activeWindowChanged,
                this, &WindowTasks::slotActiveWindowChanged);
        return;
    }

    if (QGuiApplication::platformName().contains(QStringLiteral("wayland"))) {
        qDebug() << "WindowTasks::start() creating WaylandWindowTasks";
        m_waylandTasks = new WaylandWindowTasks(this);
        connect(m_waylandTasks, &WaylandWindowTasks::windowAdded,
                this, &WindowTasks::windowAdded);
        connect(m_waylandTasks, &WaylandWindowTasks::windowRemoved,
                this, &WindowTasks::windowRemoved);
        connect(m_waylandTasks, &WaylandWindowTasks::windowChanged,
                this, &WindowTasks::windowChanged);
        connect(m_waylandTasks, &WaylandWindowTasks::activeWindowChanged,
                this, &WindowTasks::activeWindowChanged);
        connect(m_waylandTasks, &WaylandWindowTasks::activeChanged, this, [this]() {
            const bool nowAvailable = m_waylandTasks && m_waylandTasks->isActive();
            qDebug() << "WindowTasks: WaylandWindowTasks activeChanged nowAvailable=" << nowAvailable;
            if (m_available != nowAvailable) {
                m_available = nowAvailable;
                Q_EMIT availabilityChanged();
            }
        });
        m_waylandTasks->start();

        const bool nowAvailable = m_waylandTasks->isActive();
        qDebug() << "WindowTasks::start() wayland initial available=" << nowAvailable;
        if (m_available != nowAvailable) {
            m_available = nowAvailable;
            Q_EMIT availabilityChanged();
        }
    }
}

void WindowTasks::slotWindowAdded(WId id)
{
    Q_EMIT windowAdded(static_cast<quint64>(id));
}

void WindowTasks::slotWindowRemoved(WId id)
{
    Q_EMIT windowRemoved(static_cast<quint64>(id));
}

void WindowTasks::slotWindowChanged(WId id, const NET::Properties &props, const NET::Properties2 &props2)
{
    Q_UNUSED(props);
    Q_UNUSED(props2);
    Q_EMIT windowChanged(static_cast<quint64>(id));
}

void WindowTasks::slotActiveWindowChanged(WId id)
{
    m_activeWindow = static_cast<quint64>(id);
    Q_EMIT activeWindowChanged(m_activeWindow);
}

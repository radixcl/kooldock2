// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "windowtasks.h"

#include <KWindowSystem>
#include <KX11Extras>

WindowTasks::WindowTasks(QObject *parent)
    : QObject(parent)
{
    m_available = (KWindowSystem::isPlatformX11() && KX11Extras::self());
}

bool WindowTasks::isAvailable() const
{
    return m_available;
}

void WindowTasks::start()
{
    if (!m_available) {
        // TODO: Wayland taskbar via org_kde_plasma_window_management protocol
        // (Qt6Wayland client extension). For now, only launchers are shown on Wayland.
        return;
    }

    connect(KX11Extras::self(), &KX11Extras::windowAdded,
            this, &WindowTasks::slotWindowAdded);
    connect(KX11Extras::self(), &KX11Extras::windowRemoved,
            this, &WindowTasks::slotWindowRemoved);
    connect(KX11Extras::self(), &KX11Extras::windowChanged,
            this, &WindowTasks::slotWindowChanged);
    connect(KX11Extras::self(), &KX11Extras::activeWindowChanged,
            this, &WindowTasks::slotActiveWindowChanged);
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
    Q_EMIT activeWindowChanged(static_cast<quint64>(id));
}

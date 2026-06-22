// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "windowactions.h"

#include <KWindowInfo>
#include <KWindowSystem>
#include <KX11Extras>
#include <netwm.h>

#include <QGuiApplication>
#include <qnativeinterface.h>

static xcb_connection_t *x11Connection()
{
    auto *x11App = qApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11App ? x11App->connection() : nullptr;
}

WindowActions::WindowActions(QObject *parent)
    : QObject(parent)
{
}

quint64 WindowActions::currentWindow() const { return m_current; }

void WindowActions::setCurrentWindow(quint64 id)
{
    if (m_current == id) {
        return;
    }
    m_current = id;
    Q_EMIT currentWindowChanged();
}

void WindowActions::minimize()
{
    if (!m_current || !KWindowSystem::isPlatformX11()) return;
    KX11Extras::minimizeWindow(static_cast<WId>(m_current));
}

void WindowActions::maximize()
{
    if (!m_current || !KWindowSystem::isPlatformX11()) return;
    KWindowInfo info(static_cast<WId>(m_current), NET::WMState);
    if (info.state() & NET::Max) {
        KX11Extras::clearState(static_cast<WId>(m_current), NET::Max);
    } else {
        KX11Extras::setState(static_cast<WId>(m_current), NET::Max);
    }
}

void WindowActions::restore()
{
    if (!m_current || !KWindowSystem::isPlatformX11()) return;
    KX11Extras::clearState(static_cast<WId>(m_current), NET::MaxVert | NET::MaxHoriz);
    KX11Extras::unminimizeWindow(static_cast<WId>(m_current));
    KX11Extras::activateWindow(static_cast<WId>(m_current));
}

void WindowActions::close()
{
    if (!m_current) return;
    if (auto *conn = x11Connection()) {
        NETRootInfo ri(conn, NET::CloseWindow);
        ri.closeWindowRequest(static_cast<xcb_window_t>(m_current));
    }
}

void WindowActions::shade()
{
    if (!m_current || !KWindowSystem::isPlatformX11()) return;
    KWindowInfo info(static_cast<WId>(m_current), NET::WMState);
    if (info.state() & NET::Shaded) {
        KX11Extras::clearState(static_cast<WId>(m_current), NET::Shaded);
    } else {
        KX11Extras::setState(static_cast<WId>(m_current), NET::Shaded);
    }
}

void WindowActions::toggleKeepAbove()
{
    if (!m_current || !KWindowSystem::isPlatformX11()) return;
    KWindowInfo info(static_cast<WId>(m_current), NET::WMState);
    if (info.state() & NET::KeepAbove) {
        KX11Extras::clearState(static_cast<WId>(m_current), NET::KeepAbove);
    } else {
        KX11Extras::setState(static_cast<WId>(m_current), NET::KeepAbove);
    }
}

void WindowActions::toggleKeepBelow()
{
    if (!m_current || !KWindowSystem::isPlatformX11()) return;
    KWindowInfo info(static_cast<WId>(m_current), NET::WMState);
    if (info.state() & NET::KeepBelow) {
        KX11Extras::clearState(static_cast<WId>(m_current), NET::KeepBelow);
    } else {
        KX11Extras::setState(static_cast<WId>(m_current), NET::KeepBelow);
    }
}

void WindowActions::toggleFullscreen()
{
    if (!m_current || !KWindowSystem::isPlatformX11()) return;
    KWindowInfo info(static_cast<WId>(m_current), NET::WMState);
    if (info.state() & NET::FullScreen) {
        KX11Extras::clearState(static_cast<WId>(m_current), NET::FullScreen);
    } else {
        KX11Extras::setState(static_cast<WId>(m_current), NET::FullScreen);
    }
}

void WindowActions::sendToDesktop(int desktop)
{
    if (!m_current || !KWindowSystem::isPlatformX11()) return;
    if (desktop == 0) desktop = NET::OnAllDesktops;
    KX11Extras::setOnDesktop(static_cast<WId>(m_current), desktop);
}

void WindowActions::activate()
{
    if (!m_current || !KWindowSystem::isPlatformX11()) return;
    KX11Extras::activateWindow(static_cast<WId>(m_current));
}

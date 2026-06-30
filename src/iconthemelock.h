// SPDX-FileCopyrightText: 2025 Matias Fernandez <matias.fernandez@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QMutex>

// QIcon theme lookups (QIcon::fromTheme / QIcon::hasThemeIcon) funnel into the
// shared KIconLoader::global() singleton, which is NOT thread-safe. The QML
// image provider (image://kicon) resolves icons on a QtQuick worker thread,
// while window events (taskData, resolveIconName) resolve them on the GUI
// thread. When a window appears both fire at once and race inside
// KIconLoader::loadScaledIcon → SIGSEGV. Every theme-icon access must hold
// this single lock.
inline QMutex &iconThemeMutex()
{
    static QMutex mutex;
    return mutex;
}

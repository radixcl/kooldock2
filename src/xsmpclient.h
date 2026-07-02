// SPDX-FileCopyrightText: 2026 Matias Fernandez <matias.fernandez@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Registers with ksmserver over XSMP so Plasma logout can tell us to quit.
// See xsmpclient.cpp for why a Wayland app needs an X session-management
// protocol client at all. No-op when there is no session manager or when
// running on X11 (where Qt's xcb platform plugin already speaks XSMP).
void installXsmpClient();

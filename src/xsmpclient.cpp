// SPDX-FileCopyrightText: 2026 Matias Fernandez <matias.fernandez@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "xsmpclient.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QSocketNotifier>

#include <X11/ICE/ICElib.h>
#include <X11/SM/SMlib.h>

#include <cstdlib>
#include <cstring>

// Why a Wayland dock needs an XSMP client:
//
// Plasma logout runs in phases (plasma-shutdown/shutdown.cpp): first ksmserver
// closes its XSMP clients, then KWin's closeWaylandWindows() asks every
// xdg_toplevel to close and waits for all of them. Our dock window is an
// xdg_toplevel with the org_kde_plasma_shell Panel role, which KWin classifies
// as a Dock — and XdgToplevelWindow::closeWindow() is a no-op for docks
// (!isCloseable()), yet closeWaylandWindows() still puts the window on its
// wait list. Nothing ever tells us to quit, KWin waits on a close it never
// sent, and after 10s the user gets "KoolDock did not close / Log Out Anyway".
// SIGTERM only arrives later, when systemd stops graphical-session.target.
//
// Registering with ksmserver over XSMP (which Plasma still runs, Wayland or
// not) puts us in the *first* phase instead: on logout ksmserver sends
// SaveYourself then — only once the logout is committed, so cancelling is
// safe — Die. We quit, our surfaces vanish, and by the time KWin closes
// windows there is nothing of ours to wait for.

namespace
{

SmcConn s_conn = nullptr;

// Qt event loop integration: libICE hands us the connection fd; pump
// IceProcessMessages whenever it becomes readable.
void iceWatch(IceConn ice, IcePointer /*clientData*/, Bool opening, IcePointer *watchData)
{
    if (opening) {
        auto *notifier = new QSocketNotifier(IceConnectionNumber(ice), QSocketNotifier::Read,
                                             QCoreApplication::instance());
        QObject::connect(notifier, &QSocketNotifier::activated, notifier, [ice]() {
            IceProcessMessages(ice, nullptr, nullptr);
        });
        *watchData = notifier;
    } else {
        // Reached from inside the notifier's own activated handler (die →
        // SmcCloseConnection → here), so deleteLater, not delete.
        auto *notifier = static_cast<QSocketNotifier *>(*watchData);
        notifier->setEnabled(false);
        notifier->deleteLater();
    }
}

void saveYourself(SmcConn conn, SmPointer /*clientData*/, int /*saveType*/, Bool /*shutdown*/,
                  int /*interactStyle*/, Bool /*fast*/)
{
    // Nothing to save — settings are written as they change.
    SmcSaveYourselfDone(conn, True);
}

void die(SmcConn conn, SmPointer /*clientData*/)
{
    SmcCloseConnection(conn, 0, nullptr);
    s_conn = nullptr;
    // Same clean path as the Quit menu item: teardown() runs on aboutToQuit.
    QCoreApplication::quit();
}

void saveComplete(SmcConn /*conn*/, SmPointer /*clientData*/)
{
}

void shutdownCancelled(SmcConn /*conn*/, SmPointer /*clientData*/)
{
    // We already answered SaveYourselfDone; keep running.
}

} // namespace

void installXsmpClient()
{
    if (!qEnvironmentVariableIsSet("SESSION_MANAGER")) {
        return;
    }
    // On X11 Qt's xcb plugin registers with the session manager itself;
    // a second registration would just make ksmserver track us twice.
    if (QGuiApplication::platformName() != QLatin1String("wayland")) {
        return;
    }

    IceAddConnectionWatch(iceWatch, nullptr);

    SmcCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.save_yourself.callback = saveYourself;
    callbacks.die.callback = die;
    callbacks.save_complete.callback = saveComplete;
    callbacks.shutdown_cancelled.callback = shutdownCancelled;

    char *clientId = nullptr;
    char errBuf[256] = {};
    s_conn = SmcOpenConnection(nullptr, nullptr, SmProtoMajor, SmProtoMinor,
                               SmcSaveYourselfProcMask | SmcDieProcMask
                                   | SmcSaveCompleteProcMask | SmcShutdownCancelledProcMask,
                               &callbacks,
                               nullptr /*previousId*/, &clientId, sizeof(errBuf), errBuf);
    if (!s_conn) {
        IceRemoveConnectionWatch(iceWatch, nullptr);
        qWarning("kooldock2: could not connect to the session manager: %s", errBuf);
        return;
    }
    free(clientId);

    // RestartNever: ksmserver must not respawn us at login (the XDG autostart
    // entry owns that) nor record us in saved sessions. Program/UserID are
    // the protocol's required identification properties.
    char restartHint = SmRestartNever;
    SmPropValue hintVal{1, &restartHint};
    SmProp hintProp{const_cast<char *>(SmRestartStyleHint), const_cast<char *>(SmCARD8), 1, &hintVal};

    QByteArray program = QCoreApplication::applicationName().toLocal8Bit();
    SmPropValue programVal{int(program.size()), program.data()};
    SmProp programProp{const_cast<char *>(SmProgram), const_cast<char *>(SmARRAY8), 1, &programVal};

    QByteArray user = qgetenv("USER");
    SmPropValue userVal{int(user.size()), user.data()};
    SmProp userProp{const_cast<char *>(SmUserID), const_cast<char *>(SmARRAY8), 1, &userVal};

    SmProp *props[] = {&hintProp, &programProp, &userProp};
    SmcSetProperties(s_conn, 3, props);
}

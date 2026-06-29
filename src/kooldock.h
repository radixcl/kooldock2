// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <matias.fernandez@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef KOOLDOCK_H
#define KOOLDOCK_H

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQuickView>
#include <QRegion>
#include <QSize>
#include <QTimer>

#include <KSharedConfig>

#include "dockmodel.h"
#include "windowactions.h"

class WindowTasks;
class KConfigDialog;

namespace KWayland {
namespace Client {
class PlasmaShell;
class PlasmaShellSurface;
}
}

namespace LayerShellQt {
class Window;
}
class QRasterWindow;

class KoolDock : public QObject
{
    Q_OBJECT
    Q_PROPERTY(DockModel *model READ model CONSTANT)
    Q_PROPERTY(WindowActions *windowActions READ windowActions CONSTANT)
    Q_PROPERTY(Qt::Edge screenEdge READ screenEdge NOTIFY screenEdgeChanged)
    Q_PROPERTY(bool autoHide READ autoHide NOTIFY autoHideChanged)
    Q_PROPERTY(bool containsMouse READ containsMouse NOTIFY containsMouseChanged)
    Q_PROPERTY(bool dragActive READ dragActive NOTIFY dragActiveChanged)
    // True while a file drag is hovering over any dock icon's drop target.
    // State-based (not the movement heartbeat), so it stays true when the
    // cursor is held still over an icon to aim a drop.
    Q_PROPERTY(bool fileDragOver READ fileDragOver NOTIFY fileDragOverChanged)
    Q_PROPERTY(QString themeName READ themeName NOTIFY themeNameChanged)
    Q_PROPERTY(bool trashIsEmpty READ isTrashEmpty NOTIFY trashIsEmptyChanged)
    Q_PROPERTY(bool debugBounds READ debugBounds CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QStringList screenNames READ screenNames NOTIFY screenNamesChanged)
    Q_PROPERTY(QString screenName READ screenName WRITE setScreenName NOTIFY screenNameChanged)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY autostartChanged)

public:
    explicit KoolDock(QObject *parent = nullptr, bool debugBounds = false);
    ~KoolDock() override;

    static KoolDock *instance();
    static void create(QObject *parent, bool showPreferences, bool debugBounds = false);

    // Quit when the compositor asks the dock window to close. On Wayland
    // logout, KWin sends each toplevel an xdg_toplevel close request; a
    // normal app (Konsole, KWrite) quits when its last window closes and so
    // gets out of the session manager's way. The dock sets
    // quitOnLastWindowClosed(false) (so a stray close never kills it mid-use)
    // and its surfaces are a privileged plasma-shell Panel plus a layer-shell
    // spacer the compositor can't even close -- so without this it lingers
    // and stalls logout. Treat a Close on m_view as "session wants us gone"
    // and quit() (which tears down both surfaces). Nothing sends a panel a
    // close during normal use, so day-to-day behaviour is unchanged.
    bool eventFilter(QObject *watched, QEvent *event) override;

    DockModel *model() const;
    WindowActions *windowActions() const;
    Qt::Edge screenEdge() const;
    bool autoHide() const;
    bool containsMouse() const;
    bool dragActive() const;
    bool fileDragOver() const { return m_iconDragCount > 0; }
    // Called by each icon's drop target as a file drag enters/leaves it. Uses
    // a counter so moving between icons (leave-old after enter-new) never
    // flickers the aggregate off.
    Q_INVOKABLE void setIconDragOver(bool over);
    bool isTrashEmpty() const;
    bool debugBounds() const;
    QString version() const;
    QString themeName() const;
    QStringList screenNames() const;
    QString screenName() const;
    void setScreenName(const QString &name);
    bool autostart() const;
    void setAutostart(bool enable);

public Q_SLOTS:
    Q_INVOKABLE void setContainsMouse(bool contains);
    Q_INVOKABLE void setDragActive(bool active);
    Q_INVOKABLE void setDragExpanded(bool expanded);
    Q_INVOKABLE void setMinimizedGeometry(quint64 windowId, int x, int y, int w, int h);
    Q_INVOKABLE void unsetMinimizedGeometry(quint64 windowId);
    // How far beyond the icon footprint the currently-visible in-scene
    // tooltip needs, along the dock's short axis — 0 when none is
    // showing. Grows the real window immediately (no animation to race
    // against); shrinking is delayed to outlast the tooltip's 150ms
    // fade-out, same reasoning as the hover-driven m_shrinkTimer.
    Q_INVOKABLE void setTooltipExtent(int px);
    // Pointer's current distance from the screen-anchored edge along the
    // dock's short axis (always >= 0 while hovering, regardless of which
    // edge the dock is on — frame-invariant, unlike a raw local
    // coordinate). -1 while not hovering. See its use in the tooltip
    // shrink timer for why this needs to be frame-invariant.
    Q_INVOKABLE void setPointerDistanceFromEdge(qreal distance);
    Q_INVOKABLE void updateBlurRegion(qreal longPos, qreal longLength, qreal shortOffset, qreal radius);
    Q_INVOKABLE void showAppMenu();
    // Open KWin's Window View (present-windows) effect for the given KWin
    // window UUIDs — the "peek" at a grouped icon's windows. No-op if empty.
    Q_INVOKABLE void peekWindows(const QStringList &uuids);
    Q_INVOKABLE void openTrash();
    Q_INVOKABLE void trashFiles(const QVariantList &urls);
    Q_INVOKABLE void emptyTrash();
    void reload();
    void showPreferences();
    void quit();
    void toggleOrientation();
    void setScreenEdge(Qt::Edge edge);

Q_SIGNALS:
    void screenEdgeChanged();
    void autoHideChanged();
    void containsMouseChanged();
    void dragActiveChanged();
    void fileDragOverChanged();
    void trashIsEmptyChanged();
    void themeNameChanged();
    void screenNamesChanged();
    void screenNameChanged();
    void autostartChanged();

private Q_SLOTS:
    void onScreenChanged(QScreen *screen);
    void onHideTimer();
    void updateTrashState();

private:
    void setupView();
    void applyPanelShell();
    // Creates/updates or hides the invisible layer-shell "spacer" surface
    // that reserves screen space via its exclusive zone. Shown only when
    // reserveSpace is on and auto-hide is off; the dock window itself
    // (org_kde_plasma_shell) cannot reserve space, so this separate surface
    // does it. Idempotent -- safe to call from reconfigure().
    void applySpacer();
    void applyGeometry();
    void applyBlur();
    // Pushes the pending blur region to KWin at most once per throttle
    // interval, driven by QQuickWindow::afterAnimating so each update
    // rides the same gui-thread frame that produced the matching pill
    // geometry (rather than a free-running timer phase-drifting against
    // vsync — the cause of the intermittent full-screen blur flash). The
    // throttle timer is the trailing-flush fallback for when rendering
    // goes idle before another frame is produced.
    void flushBlur();
    void applyInputMask(bool hidden);
    void reconfigure();
    void refreshScreens();
    int maxDockWidth() const;
    int maxDockHeight() const;
    int maxDockLongSize() const;
    int maxDockShortSize() const;

    QPointer<QQuickView> m_view;
    KWayland::Client::PlasmaShell *m_plasmaShell = nullptr;
    KWayland::Client::PlasmaShellSurface *m_panelSurface = nullptr;
    QSize m_desiredPanelSize;
    QPointer<QRasterWindow> m_spacerView;
    LayerShellQt::Window *m_spacerLayer = nullptr;
    WindowTasks *m_tasks;
    DockModel *m_model;
    WindowActions *m_windowActions;
    KSharedConfig::Ptr m_config;
    bool m_containsMouse = false;
    bool m_dragActive = false;
    int m_iconDragCount = 0;
    bool m_dragExpanded = false;
    bool m_debugBounds = false;
    int m_tooltipExtent = 0;
    int m_pendingTooltipExtent = 0;
    qreal m_pointerDistanceFromEdge = -1;
    qreal m_blurPos = 0;
    qreal m_blurLength = 0;
    qreal m_blurShortOffset = 0;
    qreal m_blurRadius = 0;
    bool m_blurDirty = false;
    // Last region/state actually pushed to KWin, so applyBlur() can skip
    // redundant enableBlurBehind() calls (each one makes KWin regenerate
    // the blurred backbuffer; churning it every frame is what made KWin
    // glitch the blur full-screen for a frame). m_lastBlurState: -1
    // unknown, 0 disabled, 1 enabled.
    QRegion m_lastBlurRegion;
    int m_lastBlurState = -1;
    // Time since the last enableBlurBehind() push, for the flushBlur()
    // rate limit (~30 fps — KWin can't regenerate the blur at 60 fps).
    QElapsedTimer m_blurThrottle;
    QTimer m_hideTimer;
    QTimer m_shrinkTimer;
    QTimer m_tooltipShrinkTimer;
    QTimer m_blurTimer;
    QTimer m_dragHeartbeat;
    QTimer m_trashCheckTimer;
    bool m_trashIsEmpty = true;
    QStringList m_screenNames;
};

#endif

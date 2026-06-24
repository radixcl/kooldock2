// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef KOOLDOCK_H
#define KOOLDOCK_H

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQuickView>
#include <QTimer>

#include <KSharedConfig>

#include "dockmodel.h"
#include "windowactions.h"

class WindowTasks;
class KConfigDialog;

namespace LayerShellQt { class Window; }

class KoolDock : public QObject
{
    Q_OBJECT
    Q_PROPERTY(DockModel *model READ model CONSTANT)
    Q_PROPERTY(WindowActions *windowActions READ windowActions CONSTANT)
    Q_PROPERTY(Qt::Edge screenEdge READ screenEdge NOTIFY screenEdgeChanged)
    Q_PROPERTY(bool autoHide READ autoHide NOTIFY autoHideChanged)
    Q_PROPERTY(bool containsMouse READ containsMouse NOTIFY containsMouseChanged)
    Q_PROPERTY(bool dragActive READ dragActive NOTIFY dragActiveChanged)
    Q_PROPERTY(QString themeName READ themeName NOTIFY themeNameChanged)
    Q_PROPERTY(bool trashIsEmpty READ isTrashEmpty NOTIFY trashIsEmptyChanged)
    Q_PROPERTY(bool debugBounds READ debugBounds CONSTANT)

public:
    explicit KoolDock(QObject *parent = nullptr, bool debugBounds = false);
    ~KoolDock() override;

    static KoolDock *instance();
    static void create(QObject *parent, bool showPreferences, bool debugBounds = false);

    DockModel *model() const;
    WindowActions *windowActions() const;
    Qt::Edge screenEdge() const;
    bool autoHide() const;
    bool containsMouse() const;
    bool dragActive() const;
    bool isTrashEmpty() const;
    bool debugBounds() const;
    QString themeName() const;

public Q_SLOTS:
    Q_INVOKABLE void setContainsMouse(bool contains);
    Q_INVOKABLE void setDragActive(bool active);
    Q_INVOKABLE void setDragExpanded(bool expanded);
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
    void trashIsEmptyChanged();
    void themeNameChanged();

private Q_SLOTS:
    void onScreenChanged(QScreen *screen);
    void onHideTimer();
    void updateTrashState();

private:
    void setupView();
    void applyLayerShell();
    void applyGeometry();
    void applyBlur();
    void applyInputMask(bool hidden);
    void reconfigure();
    int maxDockWidth() const;
    int maxDockHeight() const;
    int maxDockLongSize() const;
    int maxDockShortSize() const;

    QPointer<QQuickView> m_view;
    LayerShellQt::Window *m_layer = nullptr;
    WindowTasks *m_tasks;
    DockModel *m_model;
    WindowActions *m_windowActions;
    KSharedConfig::Ptr m_config;
    bool m_containsMouse = false;
    bool m_dragActive = false;
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
    QTimer m_hideTimer;
    QTimer m_shrinkTimer;
    QTimer m_tooltipShrinkTimer;
    QTimer m_blurTimer;
    QTimer m_dragHeartbeat;
    QTimer m_trashCheckTimer;
    bool m_trashIsEmpty = true;
};

#endif

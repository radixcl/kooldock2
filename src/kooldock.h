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

public:
    explicit KoolDock(QObject *parent = nullptr);
    ~KoolDock() override;

    static KoolDock *instance();
    static void create(QObject *parent, bool showPreferences);

    DockModel *model() const;
    WindowActions *windowActions() const;
    Qt::Edge screenEdge() const;
    bool autoHide() const;
    bool containsMouse() const;
    bool dragActive() const;
    QString themeName() const;

public Q_SLOTS:
    Q_INVOKABLE void setContainsMouse(bool contains);
    Q_INVOKABLE void setDragActive(bool active);
    Q_INVOKABLE void updateBlurRegion(qreal longPos, qreal longLength, qreal shortOffset, qreal radius);
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
    void themeNameChanged();

private Q_SLOTS:
    void onScreenChanged(QScreen *screen);
    void onHideTimer();

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
    qreal m_blurPos = 0;
    qreal m_blurLength = 0;
    qreal m_blurShortOffset = 0;
    qreal m_blurRadius = 0;
    QTimer m_hideTimer;
    QTimer m_dragHeartbeat;
};

#endif

// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef KOOLDOCK_H
#define KOOLDOCK_H

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQuickView>

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
    QString themeName() const;

public Q_SLOTS:
    Q_INVOKABLE void setContainsMouse(bool contains);
    Q_INVOKABLE void updateBlurRegion(qreal x, qreal width, qreal radius);
    void reload();
    void showPreferences();
    void quit();
    void toggleOrientation();
    void setScreenEdge(Qt::Edge edge);

Q_SIGNALS:
    void screenEdgeChanged();
    void autoHideChanged();
    void containsMouseChanged();
    void themeNameChanged();

private Q_SLOTS:
    void onScreenChanged(QScreen *screen);

private:
    void setupView();
    void applyLayerShell();
    void applyGeometry();
    void applyBlur();
    void reconfigure();
    int maxDockWidth() const;
    int maxDockHeight() const;

    QPointer<QQuickView> m_view;
    LayerShellQt::Window *m_layer = nullptr;
    WindowTasks *m_tasks;
    DockModel *m_model;
    WindowActions *m_windowActions;
    KSharedConfig::Ptr m_config;
    bool m_containsMouse = false;
    qreal m_blurX = 0;
    qreal m_blurWidth = 0;
    qreal m_blurRadius = 0;
};

#endif

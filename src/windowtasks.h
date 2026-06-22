// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef WINDOWTASKS_H
#define WINDOWTASKS_H

#include <QObject>

#include <netwm_def.h>

#include <QWidgetList> // For WId

class WindowTasks : public QObject
{
    Q_OBJECT
public:
    explicit WindowTasks(QObject *parent = nullptr);

    bool isAvailable() const;

public Q_SLOTS:
    void start();

Q_SIGNALS:
    void windowAdded(quint64 windowId);
    void windowRemoved(quint64 windowId);
    void windowChanged(quint64 windowId);
    void activeWindowChanged(quint64 windowId);
    void availabilityChanged();

private Q_SLOTS:
    void slotWindowAdded(WId id);
    void slotWindowRemoved(WId id);
    void slotWindowChanged(WId id, const NET::Properties &props, const NET::Properties2 &props2);
    void slotActiveWindowChanged(WId id);

private:
    bool m_available = false;
};

#endif

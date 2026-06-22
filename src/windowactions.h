// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef WINDOWACTIONS_H
#define WINDOWACTIONS_H

#include <QObject>

class WindowTasks;

class WindowActions : public QObject
{
    Q_OBJECT
    Q_PROPERTY(quint64 currentWindow READ currentWindow WRITE setCurrentWindow NOTIFY currentWindowChanged)

public:
    explicit WindowActions(WindowTasks *tasks, QObject *parent = nullptr);

    quint64 currentWindow() const;
    void setCurrentWindow(quint64 id);

public Q_SLOTS:
    void minimize();
    void maximize();
    void restore();
    void close();
    void shade();
    void toggleKeepAbove();
    void toggleKeepBelow();
    void toggleFullscreen();
    void sendToDesktop(int desktop);
    void activate();

Q_SIGNALS:
    void currentWindowChanged();

private:
    quint64 m_current = 0;
    WindowTasks *m_tasks = nullptr;
};

#endif

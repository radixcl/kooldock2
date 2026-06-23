// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef ITEM_H
#define ITEM_H

#include <QObject>
#include <QString>

class Item : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString iconName READ iconName WRITE setIconName NOTIFY iconNameChanged)
    Q_PROPERTY(QString command READ command WRITE setCommand NOTIFY commandChanged)
    Q_PROPERTY(QString desktopFile READ desktopFile WRITE setDesktopFile NOTIFY desktopFileChanged)
    Q_PROPERTY(quint64 windowId READ windowId WRITE setWindowId NOTIFY windowIdChanged)
    Q_PROPERTY(bool isLauncher READ isLauncher CONSTANT)
    Q_PROPERTY(bool isTask READ isTask CONSTANT)
    Q_PROPERTY(bool isAppMenu READ isAppMenu CONSTANT)
    Q_PROPERTY(bool isTrash READ isTrash CONSTANT)
    Q_PROPERTY(bool isActive READ isActive WRITE setActive NOTIFY isActiveChanged)
    Q_PROPERTY(bool isMinimized READ isMinimized WRITE setMinimized NOTIFY isMinimizedChanged)
    Q_PROPERTY(bool isRunning READ isRunning WRITE setRunning NOTIFY isRunningChanged)
    Q_PROPERTY(int itemIndex READ itemIndex WRITE setItemIndex NOTIFY itemIndexChanged)

public:
    enum class Kind { Launcher, Task, AppMenu, Trash };

    explicit Item(QObject *parent = nullptr) : QObject(parent) {}
    Item(Kind kind, const QString &name, const QString &iconName, const QString &command, quint64 windowId = 0)
        : QObject(nullptr), m_kind(kind), m_name(name), m_iconName(iconName), m_command(command), m_windowId(windowId) {}

    Kind kind() const { return m_kind; }
    QString name() const { return m_name; }
    QString iconName() const { return m_iconName; }
    QString command() const { return m_command; }
    QString desktopFile() const { return m_desktopFile; }
    quint64 windowId() const { return m_windowId; }
    bool isTask() const { return m_kind == Kind::Task; }
    bool isLauncher() const { return m_kind == Kind::Launcher; }
    bool isAppMenu() const { return m_kind == Kind::AppMenu; }
    bool isTrash() const { return m_kind == Kind::Trash; }
    bool isActive() const { return m_isActive; }
    bool isMinimized() const { return m_isMinimized; }
    bool isRunning() const { return m_isRunning; }
    int itemIndex() const { return m_itemIndex; }

    void setName(const QString &n) { if (m_name != n) { m_name = n; Q_EMIT nameChanged(); } }
    void setIconName(const QString &n) { if (m_iconName != n) { m_iconName = n; Q_EMIT iconNameChanged(); } }
    void setCommand(const QString &c) { if (m_command != c) { m_command = c; Q_EMIT commandChanged(); } }
    void setDesktopFile(const QString &p) { if (m_desktopFile != p) { m_desktopFile = p; Q_EMIT desktopFileChanged(); } }
    void setWindowId(quint64 id) { if (m_windowId != id) { m_windowId = id; Q_EMIT windowIdChanged(); } }
    void setActive(bool on) { if (m_isActive != on) { m_isActive = on; Q_EMIT isActiveChanged(); } }
    void setMinimized(bool on) { if (m_isMinimized != on) { m_isMinimized = on; Q_EMIT isMinimizedChanged(); } }
    void setRunning(bool on) { if (m_isRunning != on) { m_isRunning = on; Q_EMIT isRunningChanged(); } }
    void setItemIndex(int i) { if (m_itemIndex != i) { m_itemIndex = i; Q_EMIT itemIndexChanged(); } }

Q_SIGNALS:
    void nameChanged();
    void iconNameChanged();
    void commandChanged();
    void desktopFileChanged();
    void windowIdChanged();
    void isActiveChanged();
    void isMinimizedChanged();
    void isRunningChanged();
    void itemIndexChanged();

private:
    Kind m_kind = Kind::Launcher;
    QString m_name;
    QString m_iconName;
    QString m_command;
    QString m_desktopFile;
    quint64 m_windowId = 0;
    bool m_isActive = false;
    bool m_isMinimized = false;
    bool m_isRunning = false;
    int m_itemIndex = -1;
};

#endif

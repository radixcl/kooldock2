// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef ITEM_H
#define ITEM_H

#include <QObject>
#include <QString>
#include <QVariant>

class Item : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString iconName READ iconName WRITE setIconName NOTIFY iconNameChanged)
    Q_PROPERTY(QString command READ command WRITE setCommand NOTIFY commandChanged)
    Q_PROPERTY(QString desktopFile READ desktopFile WRITE setDesktopFile NOTIFY desktopFileChanged)
    Q_PROPERTY(QString appId READ appId WRITE setAppId NOTIFY appIdChanged)
    Q_PROPERTY(quint64 windowId READ windowId WRITE setWindowId NOTIFY windowIdChanged)
    Q_PROPERTY(int windowCount READ windowCount NOTIFY windowCountChanged)
    Q_PROPERTY(QVariantList windowList READ windowList NOTIFY windowListChanged)
    Q_PROPERTY(bool isLauncher READ isLauncher CONSTANT)
    Q_PROPERTY(bool isTask READ isTask CONSTANT)
    Q_PROPERTY(bool isAppMenu READ isAppMenu CONSTANT)
    Q_PROPERTY(bool isTrash READ isTrash CONSTANT)
    Q_PROPERTY(bool isActive READ isActive WRITE setActive NOTIFY isActiveChanged)
    Q_PROPERTY(bool isMinimized READ isMinimized WRITE setMinimized NOTIFY isMinimizedChanged)
    Q_PROPERTY(bool isRunning READ isRunning WRITE setRunning NOTIFY isRunningChanged)
    Q_PROPERTY(int itemIndex READ itemIndex WRITE setItemIndex NOTIFY itemIndexChanged)
    Q_PROPERTY(int badgeCount READ badgeCount WRITE setBadgeCount NOTIFY badgeCountChanged)

public:
    enum class Kind { Launcher, Task, AppMenu, Trash };

    explicit Item(QObject *parent = nullptr) : QObject(parent) {}
    Item(Kind kind, const QString &name, const QString &iconName, const QString &command, quint64 windowId = 0)
        : QObject(nullptr), m_kind(kind), m_name(name), m_iconName(iconName), m_command(command), m_windowId(windowId)
    {
        if (windowId) {
            m_groupedWindowIds.append(windowId);
            m_windowCount = 1;
        }
    }

    Kind kind() const { return m_kind; }
    QString name() const { return m_name; }
    QString iconName() const { return m_iconName; }
    QString command() const { return m_command; }
    QString desktopFile() const { return m_desktopFile; }
    QString appId() const { return m_appId; }
    quint64 windowId() const { return m_windowId; }
    bool isTask() const { return m_kind == Kind::Task; }
    bool isLauncher() const { return m_kind == Kind::Launcher; }
    bool isAppMenu() const { return m_kind == Kind::AppMenu; }
    bool isTrash() const { return m_kind == Kind::Trash; }
    bool isActive() const { return m_isActive; }
    bool isMinimized() const { return m_isMinimized; }
    bool isRunning() const { return m_isRunning; }
    int itemIndex() const { return m_itemIndex; }
    int badgeCount() const { return m_badgeCount; }
    int windowCount() const { return m_windowCount; }
    QVariantList windowList() const
    {
        QVariantList list;
        list.reserve(m_groupedWindowIds.size());
        for (quint64 id : m_groupedWindowIds) {
            list.append(QVariant::fromValue(id));
        }
        return list;
    }
    QList<quint64> groupedWindowIds() const { return m_groupedWindowIds; }

    void addGroupedWindowId(quint64 id)
    {
        if (!m_groupedWindowIds.contains(id)) {
            m_groupedWindowIds.append(id);
            m_windowCount = m_groupedWindowIds.size();
            m_windowId = id;
            Q_EMIT windowCountChanged();
            Q_EMIT windowListChanged();
            Q_EMIT windowIdChanged();
        }
    }
    bool isGrouped() const { return m_isRunning && m_windowCount > 1; }

    bool removeGroupedWindowId(quint64 id)
    {
        const bool removed = m_groupedWindowIds.removeOne(id);
        if (removed) {
            m_windowCount = m_groupedWindowIds.size();
            if (!m_groupedWindowIds.isEmpty()) {
                m_windowId = m_groupedWindowIds.last();
            } else {
                m_windowId = 0;
            }
            Q_EMIT windowCountChanged();
            Q_EMIT windowListChanged();
            Q_EMIT windowIdChanged();
        }
        return removed;
    }

    quint64 nextWindowId() const
    {
        if (m_groupedWindowIds.size() <= 1) return m_windowId;
        const int idx = m_groupedWindowIds.indexOf(m_windowId);
        const int next = (idx + 1) % m_groupedWindowIds.size();
        return m_groupedWindowIds.at(next);
    }

    void setPrimaryWindowId(quint64 id)
    {
        if (m_groupedWindowIds.contains(id) && m_windowId != id) {
            m_windowId = id;
            Q_EMIT windowIdChanged();
        }
    }

    void setName(const QString &n) { if (m_name != n) { m_name = n; Q_EMIT nameChanged(); } }
    void setIconName(const QString &n) { if (m_iconName != n) { m_iconName = n; Q_EMIT iconNameChanged(); } }
    void setCommand(const QString &c) { if (m_command != c) { m_command = c; Q_EMIT commandChanged(); } }
    void setDesktopFile(const QString &p) { if (m_desktopFile != p) { m_desktopFile = p; Q_EMIT desktopFileChanged(); } }
    void setAppId(const QString &id) { if (m_appId != id) { m_appId = id; Q_EMIT appIdChanged(); } }
    void setWindowId(quint64 id) {
        if (m_windowId != id) {
            m_windowId = id;
            if (id && !m_groupedWindowIds.contains(id)) {
                m_groupedWindowIds.append(id);
                m_windowCount = m_groupedWindowIds.size();
                Q_EMIT windowCountChanged();
                Q_EMIT windowListChanged();
            }
            Q_EMIT windowIdChanged();
        }
    }
    void setActive(bool on) { if (m_isActive != on) { m_isActive = on; Q_EMIT isActiveChanged(); } }
    void setMinimized(bool on) { if (m_isMinimized != on) { m_isMinimized = on; Q_EMIT isMinimizedChanged(); } }
    void setRunning(bool on) {
        if (m_isRunning != on) {
            m_isRunning = on;
            if (!on) {
                m_groupedWindowIds.clear();
                m_windowCount = 0;
                Q_EMIT windowCountChanged();
                Q_EMIT windowListChanged();
            }
            Q_EMIT isRunningChanged();
        }
    }
    void setItemIndex(int i) { if (m_itemIndex != i) { m_itemIndex = i; Q_EMIT itemIndexChanged(); } }
    void setBadgeCount(int c) { if (m_badgeCount != c) { m_badgeCount = c; Q_EMIT badgeCountChanged(); } }

Q_SIGNALS:
    void nameChanged();
    void iconNameChanged();
    void commandChanged();
    void desktopFileChanged();
    void appIdChanged();
    void windowIdChanged();
    void isActiveChanged();
    void isMinimizedChanged();
    void isRunningChanged();
    void itemIndexChanged();
    void badgeCountChanged();
    void windowCountChanged();
    void windowListChanged();

private:
    Kind m_kind = Kind::Launcher;
    QString m_name;
    QString m_iconName;
    QString m_command;
    QString m_desktopFile;
    QString m_appId;
    quint64 m_windowId = 0;
    bool m_isActive = false;
    bool m_isMinimized = false;
    bool m_isRunning = false;
    int m_itemIndex = -1;
    int m_badgeCount = 0;
    int m_windowCount = 0;
    QList<quint64> m_groupedWindowIds;
};

#endif

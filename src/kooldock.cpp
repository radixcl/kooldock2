// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kooldock.h"

#include "dockmodel.h"
#include "kooldocksettings.h"
#include "version.h"
#include "windowactions.h"
#include "windowtasks.h"

#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/plasmashell.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/surface.h>

#include <LayerShellQt/Window>

#include <KConfigDialog>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <KWindowEffects>
#include <KWindowSystem>
#include <KIO/CopyJob>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRasterWindow>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <qnativeinterface.h>

namespace {
// The strut "spacer": a borderless, input-transparent window that commits a
// fully transparent buffer. It only exists so that, decorated as a
// layer-shell surface with an exclusive zone, KWin reserves screen space for
// the dock. A buffer must actually be committed for the layer surface to map
// and the exclusive zone to take effect, hence the painting (it just paints
// nothing visible).
class SpacerWindow : public QRasterWindow
{
public:
    SpacerWindow()
    {
        setFlag(Qt::FramelessWindowHint);
        setFlag(Qt::WindowDoesNotAcceptFocus);
        setFlag(Qt::WindowTransparentForInput);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(QRect(QPoint(0, 0), size()), Qt::transparent);
    }
};
} // namespace

static QString resolveFlatpakIcon(const QString &appId)
{
    static QHash<QString, QString> cache;

    auto it = cache.constFind(appId);
    if (it != cache.constEnd()) {
        return it->isEmpty() ? QString() : *it;
    }

    QString name = appId;
    if (name.endsWith(QLatin1String("-flatpak"), Qt::CaseInsensitive)) {
        name = name.left(name.length() - 8);
    }
    if (name.isEmpty()) {
        cache.insert(appId, QString());
        return {};
    }

    const QStringList iconDirs = {
        QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/icons/hicolor"),
        QStringLiteral("/var/lib/flatpak/exports/share/icons/hicolor"),
    };

    for (const QString &iconDir : iconDirs) {
        QDirIterator it(iconDir, QStringList() << QStringLiteral("*.png") << QStringLiteral("*.svg") << QStringLiteral("*.svgz"),
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString iconName = it.fileInfo().completeBaseName();
            if (iconName.compare(name, Qt::CaseInsensitive) == 0
                || iconName.contains(name, Qt::CaseInsensitive)) {
                cache.insert(appId, iconName);
                return iconName;
            }
        }
    }

    cache.insert(appId, QString());
    return {};
}

class IconImageProvider : public QQuickImageProvider
{
public:
    IconImageProvider() : QQuickImageProvider(Pixmap) {}
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        QIcon icon = QIcon::fromTheme(id);
        if (icon.isNull()) {
            const QString lower = id.toLower();
            if (lower != id) {
                icon = QIcon::fromTheme(lower);
            }
            if (icon.isNull()) {
                const int flatpakDash = id.indexOf(QLatin1String("-flatpak"), 0, Qt::CaseInsensitive);
                if (flatpakDash > 0) {
                    const QString stripped = id.left(flatpakDash);
                    icon = QIcon::fromTheme(stripped);
                    if (icon.isNull()) {
                        icon = QIcon::fromTheme(stripped.toLower());
                    }
                }
            }
            if (icon.isNull() && id.contains(QLatin1Char('.'))) {
                const QString last = id.section(QLatin1Char('.'), -1);
                if (!last.isEmpty()) {
                    icon = QIcon::fromTheme(last);
                    if (icon.isNull()) icon = QIcon::fromTheme(last.toLower());
                }
            }
            if (icon.isNull()) {
                const QString resolved = resolveFlatpakIcon(id);
                if (!resolved.isEmpty()) {
                    icon = QIcon::fromTheme(resolved);
                }
            }
        }
        if (icon.isNull()) icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        const QSize s = requestedSize.isValid() ? requestedSize : QSize(48, 48);
        QPixmap pix = icon.pixmap(s);
        if (size) *size = pix.size();
        return pix;
    }
};

static QPointer<KoolDock> s_instance;

KoolDock *KoolDock::instance()
{
    return s_instance;
}

void KoolDock::create(QObject *parent, bool showPreferences, bool debugBounds)
{
    Q_ASSERT(!s_instance);
    s_instance = new KoolDock(parent, debugBounds);
    if (showPreferences) {
        QTimer::singleShot(0, s_instance, &KoolDock::showPreferences);
    }
}

KoolDock::KoolDock(QObject *parent, bool debugBounds)
    : QObject(parent)
    , m_tasks(new WindowTasks(this))
    , m_model(new DockModel(m_tasks, debugBounds, this))
    , m_windowActions(new WindowActions(m_tasks, this))
    , m_config(KSharedConfig::openConfig(QStringLiteral("kooldockrc")))
    , m_debugBounds(debugBounds)
{
    connect(m_tasks, &WindowTasks::windowAdded, m_model, &DockModel::onWindowAdded);
    connect(m_tasks, &WindowTasks::windowRemoved, m_model, &DockModel::onWindowRemoved);
    connect(m_tasks, &WindowTasks::windowChanged, m_model, &DockModel::onWindowChanged);
    connect(m_tasks, &WindowTasks::activeWindowChanged, m_model, &DockModel::onActiveWindowChanged);
    connect(m_tasks, &WindowTasks::availabilityChanged, m_model, &DockModel::reload);

    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, &KoolDock::onHideTimer);

    m_shrinkTimer.setSingleShot(true);
    connect(&m_shrinkTimer, &QTimer::timeout, this, &KoolDock::applyPanelShell);

    // Throttle blur region updates: QML pushes new geometry every frame
    // (60 fps) during the zoom animation, but every enableBlurBehind
    // call tells KWin to regenerate the blurred background — KWin
    // can't keep up at 60 fps and drops the blur for a frame.  A
    // single-shot timer restarted by updateBlurRegion (and re-armed
    // by applyBlur if more updates arrived while waiting) applies the
    // latest values at most every 32 ms (~30 fps).
    m_blurTimer.setSingleShot(true);
    m_blurTimer.setInterval(32);
    connect(&m_blurTimer, &QTimer::timeout, this, &KoolDock::applyBlur);

    m_tooltipShrinkTimer.setSingleShot(true);
    // Tooltip extent is no longer used for window sizing — all tooltip
    // space is pre-reserved in maxDockShortSize().  The timer/callback
    // still exists so QML's setTooltipExtent() calls are harmless no-ops.
    connect(&m_tooltipShrinkTimer, &QTimer::timeout, this, [this]() {
        m_tooltipExtent = m_pendingTooltipExtent;
    });

    m_dragHeartbeat.setSingleShot(true);
    connect(&m_dragHeartbeat, &QTimer::timeout, this, [this]() {
        setDragActive(false);
    });

    m_trashCheckTimer.setInterval(2000);
    connect(&m_trashCheckTimer, &QTimer::timeout, this, &KoolDock::updateTrashState);
    m_trashCheckTimer.start();

    // Start window/task tracking FIRST.  The raw libwayland registry in
    // WaylandWindowTasks::start() needs to bind org_kde_plasma_window_management
    // before the KWayland::Client::Registry below creates its own registry
    // (for org_kde_plasma_shell) on the same wl_display — two simultaneous
    // dispatch mechanisms (KWayland's internal roundtrip vs raw
    // wl_display_roundtrip) can race and cause KWin to omit the privileged
    // protocol from the later registry, silently breaking task tracking.
    m_tasks->start();

    // Now bind org_kde_plasma_shell so the dock can be decorated as a
    // real panel (KWayland::Client::PlasmaShellSurface, Role::Panel) rather
    // than a generic xdg_toplevel surface -- KWin only exempts the former
    // from hiding everything during "Show Desktop". The binding itself is
    // async (it completes on the next Wayland round-trip), so spin the
    // event loop briefly here: setupView() below needs m_plasmaShell ready
    // before the window is first shown, to avoid a frame of default
    // xdg_toplevel placement before the panel position/role apply.
    using namespace KWayland::Client;
    if (auto *connection = ConnectionThread::fromApplication(this)) {
        auto *registry = new Registry(this);
        connect(registry, &Registry::plasmaShellAnnounced, this,
                [this, registry](quint32 name, quint32 version) {
                    m_plasmaShell = registry->createPlasmaShell(name, version, this);
                });
        registry->create(connection);
        registry->setup();
        for (int i = 0; i < 50 && !m_plasmaShell; ++i) {
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 20);
        }
    }
    if (!m_plasmaShell) {
        qWarning() << "KoolDock: org_kde_plasma_shell unavailable; the dock "
                       "will be hidden by KWin's \"Show Desktop\"";
    }

    setupView();

    connect(m_model, &DockModel::countChanged, this, [this]() { applyPanelShell(); });

    connect(m_model, &DockModel::activateAppMenu, this, &KoolDock::showAppMenu);
    connect(m_model, &DockModel::activateTrash, this, &KoolDock::openTrash);

    connect(KoolDockSettings::self(), &KCoreConfigSkeleton::configChanged,
            this, [this]() { reconfigure(); });
    refreshScreens();
    connect(qApp, &QGuiApplication::screenAdded,   this, [this](QScreen *) { refreshScreens(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *s) {
        refreshScreens();
        // If the configured screen was the one that disappeared, clear the
        // setting so the dock falls back to primary and re-apply the panel.
        if (KoolDockSettings::screenName() == s->name()) {
            KoolDockSettings::setScreenName(QString());
            KoolDockSettings::self()->save();
            Q_EMIT screenNameChanged();
            applyPanelShell();
        }
    });
    reconfigure();
}

KoolDock::~KoolDock()
{
    if (m_view) {
        m_view->deleteLater();
    }
}

DockModel *KoolDock::model() const { return m_model; }
WindowActions *KoolDock::windowActions() const { return m_windowActions; }

Qt::Edge KoolDock::screenEdge() const
{
    switch (KoolDockSettings::orientation()) {
    case 0: return Qt::BottomEdge;
    case 1: return Qt::LeftEdge;
    case 2: return Qt::TopEdge;
    case 3: return Qt::RightEdge;
    default: return Qt::BottomEdge;
    }
}

bool KoolDock::autoHide() const { return KoolDockSettings::autoHide(); }
bool KoolDock::containsMouse() const { return m_containsMouse; }
bool KoolDock::dragActive() const { return m_dragActive; }
bool KoolDock::debugBounds() const { return m_debugBounds; }

void KoolDock::setDragActive(bool active)
{
    if (active) {
        // Keep the heartbeat alive while drag events flow.
        m_dragHeartbeat.start(500);
    }
    if (m_dragActive == active) return;
    m_dragActive = active;
    Q_EMIT dragActiveChanged();
    // Don't call setContainsMouse(active) here: Main.qml's root.containsMouse
    // already ORs in kooldock.dragActive, so this property change propagates
    // there and back to setContainsMouse() with the correctly *combined*
    // value on its own. Calling setContainsMouse(active) directly used to
    // stomp that — e.g. ending a drag (active=false) while the cursor was
    // still over the dock forced containsMouse to false even though the
    // real hover state was (and stayed) true. Since nothing else nudges
    // root.containsMouse afterward (it never actually changed from QML's
    // point of view), nothing corrected the mismatch: the window stayed
    // shrunk until some unrelated hover transition happened to fix it.
}

void KoolDock::setDragExpanded(bool expanded)
{
    if (m_dragExpanded == expanded) return;
    m_dragExpanded = expanded;
    // Window is always full-screen; drag tracking works anywhere.
}

void KoolDock::setMinimizedGeometry(quint64 windowId, int x, int y, int w, int h)
{
    if (!m_tasks || !m_view) return;
    if (!KoolDockSettings::minimizeAnimation()) return;
    m_tasks->setMinimizedGeometry(windowId, m_view, x, y, w, h);
}

void KoolDock::unsetMinimizedGeometry(quint64 windowId)
{
    if (!m_tasks || !m_view) return;
    if (!KoolDockSettings::minimizeAnimation()) return;
    m_tasks->unsetMinimizedGeometry(windowId, m_view);
}

QString KoolDock::version() const { return QString::fromLatin1(KOOLDOCK_VERSION); }
QString KoolDock::themeName() const { return KoolDockSettings::themeName(); }

QStringList KoolDock::screenNames() const
{
    return m_screenNames;
}

QString KoolDock::screenName() const
{
    return KoolDockSettings::screenName();
}

void KoolDock::setScreenName(const QString &name)
{
    if (KoolDockSettings::screenName() == name) return;
    KoolDockSettings::setScreenName(name);
    KoolDockSettings::self()->save();
    reconfigure();
    Q_EMIT screenNameChanged();
}

// Source of truth is the presence of the file itself (the freedesktop.org
// autostart convention), not a kcfg entry — that way the checkbox always
// reflects what will actually happen on next login, even if the file was
// added/removed by something other than this dialog.
static QString autostartDesktopFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/org.kde.kooldock2.desktop");
}

bool KoolDock::autostart() const
{
    return QFile::exists(autostartDesktopFilePath());
}

void KoolDock::setAutostart(bool enable)
{
    const QString path = autostartDesktopFilePath();
    if (enable == QFile::exists(path)) return;

    if (!enable) {
        QFile::remove(path);
        Q_EMIT autostartChanged();
        return;
    }

    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                  + QStringLiteral("/autostart"));

    // Prefer copying the installed .desktop file (kept in sync with the
    // installed Exec= path); fall back to a minimal one for unpackaged
    // dev builds run straight out of build/bin.
    const QString installed = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                       QStringLiteral("org.kde.kooldock2.desktop"));
    if (!installed.isEmpty()) {
        QFile::copy(installed, path);
    } else {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(QStringLiteral(
                "[Desktop Entry]\n"
                "Type=Application\n"
                "Name=KoolDock2\n"
                "Exec=%1\n"
                "Icon=kooldock2\n"
                "X-KDE-Wayland-Interfaces=org_kde_plasma_window_management,org_kde_plasma_shell\n")
                .arg(QCoreApplication::applicationFilePath()).toUtf8());
        }
    }
    Q_EMIT autostartChanged();
}

void KoolDock::refreshScreens()
{
    QStringList names;
    for (auto *s : QGuiApplication::screens())
        names.append(s->name());
    if (names != m_screenNames) {
        m_screenNames = names;
        Q_EMIT screenNamesChanged();
    }
}

void KoolDock::setupView()
{
    m_view = new QQuickView();
    m_view->engine()->addImageProvider(QStringLiteral("kicon"), new IconImageProvider());
    m_view->setFlag(Qt::FramelessWindowHint);
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus);
    m_view->setColor(Qt::transparent);

    // Attach the PlasmaShellSurface (role + behavior) to the native window
    // before any QML loads. QQuickView::setSource() below triggers the
    // scene graph's first render/commit; the panel role needs to be
    // requested before that commit for KWin to honor it, instead of
    // treating the surface as a plain, compositor-placed toplevel.
    m_view->create();
    applyPanelShell();

    m_view->rootContext()->setContextObject(new KLocalizedContext(m_view));
    m_view->rootContext()->setContextProperty(QStringLiteral("settings"), KoolDockSettings::self());

    m_view->setSource(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    auto *root = m_view->rootObject();
    if (root) {
        root->setProperty("kooldock", QVariant::fromValue(this));
    }

    m_view->setResizeMode(QQuickView::SizeRootObjectToView);

    // Re-apply position/size now that the resize mode above is in effect.
    // QQuickView's default SizeViewToRootObject mode (in force while
    // setSource() just loaded the QML) can shrink the window to the
    // content's implicit size; without this second call that smaller size
    // would stick, leaving the full-screen-sized input mask in
    // applyInputMask() pointing at the wrong on-screen region.
    applyPanelShell();
    applyGeometry();
    // Skip blur on startup when auto-hiding: the window starts hidden
    // (a narrow trigger strip), so a blur region would show a blurred
    // rectangle at the edge with no visible pill. Blur is enabled by
    // setContainsMouse() when the dock expands.
    if (!autoHide()) {
        applyBlur();
    }

    m_view->show();
    // KWin sends its own initial xdg_toplevel configure for a freshly
    // mapped floating toplevel (sized as if for a normal decorated window),
    // which can win over the size requested above and leave a gap at the
    // anchored edge. Re-assert the exact full-screen size/position once
    // that initial configure has been processed.
    QTimer::singleShot(0, this, [this]() { applyPanelShell(); });
    if (m_debugBounds) {
        QTimer::singleShot(500, this, [this]() {
            qDebug() << "applyPanelShell (settled, +500ms): actualViewSize=" << m_view->size()
                     << "actualViewGeometry=" << m_view->geometry();
        });
    }
    if (!autoHide()) {
        QTimer::singleShot(100, this, [this]() { applyBlur(); });
    }
    applySpacer();
}

void KoolDock::applyPanelShell()
{
    if (!m_view) {
        return;
    }

    if (!m_panelSurface) {
        if (!m_plasmaShell) {
            return;
        }
        // The PlasmaShellSurface decorates an already-created wl_surface
        // (unlike layer-shell, which assigns the surface's role itself) —
        // create() allocates the native window without mapping/showing it,
        // so this can run before the first show() with no visible flicker.
        m_view->create();
        auto *surface = KWayland::Client::Surface::fromWindow(m_view);
        if (!surface) {
            qWarning() << "KoolDock: no wl_surface yet for the dock window; "
                           "panel role/position not applied this time";
            return;
        }
        m_panelSurface = m_plasmaShell->createSurface(surface, this);
        using PS = KWayland::Client::PlasmaShellSurface;
        m_panelSurface->setRole(PS::Role::Panel);
        m_panelSurface->setPanelBehavior(PS::PanelBehavior::AlwaysVisible);
        m_panelSurface->setSkipTaskbar(true);
        m_panelSurface->setSkipSwitcher(true);

        // KWin sends its own xdg_toplevel configure shortly after the
        // window is first mapped, suggesting a size as if for a normal
        // decorated floating window -- it arrives *after* the resize()
        // calls below and wins, leaving a gap at the anchored edge despite
        // setMinimumSize()==setMaximumSize(). Re-assert m_desiredPanelSize
        // every time Qt applies one of these unsolicited resizes.
        connect(m_view, &QWindow::widthChanged, this, [this](int) {
            if (m_desiredPanelSize.isValid() && m_view->size() != m_desiredPanelSize) {
                m_view->resize(m_desiredPanelSize);
            }
        });
        connect(m_view, &QWindow::heightChanged, this, [this](int) {
            if (m_desiredPanelSize.isValid() && m_view->size() != m_desiredPanelSize) {
                m_view->resize(m_desiredPanelSize);
            }
        });
    }

    // Screen selection — place the panel on the configured monitor,
    // falling back to the primary screen if none is set or the configured
    // name doesn't match any connected screen. Mirrors maxDockWidth()/
    // maxDockHeight()'s lookup.
    const QString target = KoolDockSettings::screenName();
    QScreen *chosen = QGuiApplication::primaryScreen();
    for (auto *s : QGuiApplication::screens()) {
        if (s->name() == target) {
            chosen = s;
            break;
        }
    }
    m_view->setScreen(chosen);

    // Force the exact full-screen size: a plain xdg_toplevel has no
    // equivalent to layer-shell's setDesiredSize() configure negotiation,
    // and KWin's own initial-configure size suggestion for a new floating
    // toplevel can otherwise win out over a plain resize(), leaving a gap
    // at the anchored edge. min==max==resize pins it unconditionally.
    const QSize size(maxDockWidth(), maxDockHeight());
    m_desiredPanelSize = size;
    m_view->setMinimumSize(size);
    m_view->setMaximumSize(size);
    m_view->resize(size);

    // Edge margin: a negative value pushes the dock past other panels
    // (e.g. the KDE taskbar) toward the screen edge, so the dock can sit
    // below the taskbar instead of above it. The window is always
    // full-screen sized (the pill is positioned at the anchored edge by
    // QML, the rest is transparent), so shifting the window's global
    // origin away from the anchored edge by edgeMargin moves the pill the
    // same way a layer-shell margin would.
    const int edgeMargin = KoolDockSettings::edgeMargin();
    const QRect g = chosen ? chosen->geometry() : QRect(0, 0, 1920, 1080);
    QPoint pos = g.topLeft();
    switch (screenEdge()) {
    case Qt::BottomEdge: pos.setY(g.top() - edgeMargin); break;
    case Qt::TopEdge:    pos.setY(g.top() + edgeMargin); break;
    case Qt::LeftEdge:   pos.setX(g.left() + edgeMargin); break;
    case Qt::RightEdge:  pos.setX(g.left() - edgeMargin); break;
    }
    m_panelSurface->setPosition(pos);
    // Also set the Qt-level window position so that QWindow::position()
    // and mapToGlobal() reflect the actual on-screen placement.  This is
    // what setMinimizedGeometry() relies on to translate QML-local icon
    // coordinates into the panel-relative coordinates that KWin's
    // set_minimized_geometry protocol expects.
    m_view->setPosition(pos);

    if (m_debugBounds) qDebug() << "applyPanelShell: edge=" << screenEdge()
        << "screen=" << (chosen ? chosen->name() : QStringLiteral("null"))
        << "screenGeometry=" << g
        << "requestedSize=" << size
        << "requestedPosition=" << pos
        << "edgeMargin=" << edgeMargin
        << "actualViewSize=" << m_view->size()
        << "actualViewGeometry=" << m_view->geometry()
        << "devicePixelRatio=" << m_view->devicePixelRatio();

    applyInputMask(!m_containsMouse);
}

void KoolDock::applySpacer()
{
    // The dock window uses org_kde_plasma_shell (Role::Panel), which does not
    // reserve screen space -- maximized windows draw all the way to the edge,
    // behind the dock. When the user asks for panel-like behavior (and the
    // dock isn't auto-hiding, where reserving space makes no sense), decorate
    // a separate, invisible window with wlr-layer-shell and an exclusive zone,
    // which KWin honors as a strut. The two protocols never touch the same
    // surface, so the plasma-shell "stays visible during Show Desktop"
    // behavior is unaffected.
    const bool want = KoolDockSettings::reserveSpace() && !autoHide();
    if (!want) {
        if (m_spacerView) {
            m_spacerView->hide(); // unmapping releases the reserved strut
        }
        return;
    }

    QScreen *chosen = QGuiApplication::primaryScreen();
    const QString target = KoolDockSettings::screenName();
    for (auto *s : QGuiApplication::screens()) {
        if (s->name() == target) { chosen = s; break; }
    }
    if (!chosen) {
        return;
    }
    const QRect g = chosen->geometry();

    // Reserve only the pill's band: its height plus any positive edge gap.
    // A negative edgeMargin tucks the pill past other panels toward the edge,
    // so there's no extra gap to reserve in that case.
    const int thickness = KoolDockSettings::dockHeight()
        + qMax(0, KoolDockSettings::edgeMargin());

    using LSW = LayerShellQt::Window;
    LSW::Anchors anchors;
    LSW::Anchor exclusiveEdge = LSW::AnchorNone;
    QSize size;
    switch (screenEdge()) {
    case Qt::BottomEdge:
        anchors = LSW::Anchors(LSW::AnchorBottom | LSW::AnchorLeft | LSW::AnchorRight);
        exclusiveEdge = LSW::AnchorBottom;
        size = QSize(g.width(), thickness);
        break;
    case Qt::TopEdge:
        anchors = LSW::Anchors(LSW::AnchorTop | LSW::AnchorLeft | LSW::AnchorRight);
        exclusiveEdge = LSW::AnchorTop;
        size = QSize(g.width(), thickness);
        break;
    case Qt::LeftEdge:
        anchors = LSW::Anchors(LSW::AnchorLeft | LSW::AnchorTop | LSW::AnchorBottom);
        exclusiveEdge = LSW::AnchorLeft;
        size = QSize(thickness, g.height());
        break;
    case Qt::RightEdge:
        anchors = LSW::Anchors(LSW::AnchorRight | LSW::AnchorTop | LSW::AnchorBottom);
        exclusiveEdge = LSW::AnchorRight;
        size = QSize(thickness, g.height());
        break;
    }

    if (!m_spacerView) {
        auto *win = new SpacerWindow();
        // Need an alpha channel so the committed buffer is actually
        // transparent rather than opaque black.
        QSurfaceFormat fmt = win->format();
        fmt.setAlphaBufferSize(8);
        win->setFormat(fmt);
        // The layer-shell role must be attached before the platform window
        // is created (i.e. before show()).
        m_spacerLayer = LSW::get(win);
        m_spacerLayer->setLayer(LSW::LayerTop);
        m_spacerLayer->setScope(QStringLiteral("kooldock-spacer"));
        m_spacerLayer->setKeyboardInteractivity(LSW::KeyboardInteractivityNone);
        m_spacerView = win;
    }

    m_spacerView->setScreen(chosen);
    m_spacerLayer->setAnchors(anchors);
    m_spacerLayer->setExclusiveEdge(exclusiveEdge);
    m_spacerLayer->setExclusiveZone(thickness);
    m_spacerView->resize(size);
    m_spacerView->show();
}

void KoolDock::applyGeometry()
{
    if (!m_view || !m_view->screen()) {
        return;
    }
    // The QML root sizes itself; on Wayland the layer surface anchors fix the edge.
    // We only need to suggest a default size for the very first show.
    const QSize defaultSize(maxDockWidth(), maxDockHeight());
    if (m_view->size().isEmpty()) {
        m_view->resize(defaultSize);
    }
}

int KoolDock::maxDockLongSize() const
{
    // The dock's size along its long axis (the direction icons lay out
    // and zoom along). Mirrors DockBar.qml's layout() worst case: rest
    // span plus the parabola bump for the cursor parked on one icon and
    // every neighbour still inside the falloff radius.
    const int count = m_model ? m_model->count() : 0;
    if (count <= 0) {
        return 64;
    }

    const int smallIconSize = KoolDockSettings::smallIconSize();
    const int bigIconSize = KoolDockSettings::bigIconSize();
    const int iconSpacing = KoolDockSettings::iconSpacing();
    const int zoomRange = KoolDockSettings::bigIconAmount();

    const int iDist = smallIconSize + iconSpacing;
    const int W = iDist * zoomRange / 2;
    const int H = bigIconSize - smallIconSize;

    int extraWidth = 0;
    for (int dx = 0; dx < W; dx += iDist) {
        const int bump = qMax(0, bigIconSize - (dx * dx * H) / (W * W) - smallIconSize);
        extraWidth += (dx == 0) ? bump : bump * 2;
    }

    const int restWidth = iconSpacing + count * iDist;
    return restWidth + extraWidth + iconSpacing * 2;
}

int KoolDock::maxDockShortSize() const
{
    // The dock's size along its short axis: the pill's fixed bgHeight
    // band, plus enough room on the overflow side to fit the tallest
    // possible icon plus its margin and the in-scene tooltip.
    //
    // Tooltip space is pre-reserved here so the window never resizes
    // when a tooltip appears or disappears — every resize on a layer
    // surface causes the compositor to stretch the previous buffer to
    // the new size for one frame, a visible flicker.
    //
    // On horizontal edges (Top/Bottom) the tooltip is a short label
    // above/below the icon; its height is predictable from the font
    // size (~30 px).  On vertical edges (Left/Right) the tooltip sits
    // beside the icon and its width varies with the app name length;
    // we reserve a generous fixed amount (~300 px) for typical names.
    const int bgHeight = KoolDockSettings::dockHeight();
    const int needed = KoolDockSettings::iconSpacing() + KoolDockSettings::bigIconSize() + 4;
    const int tooltipReserve = KoolDockSettings::showNames()
        ? ((screenEdge() == Qt::LeftEdge || screenEdge() == Qt::RightEdge)
            ? KoolDockSettings::tooltipSize() * 25 + 32   // vertical: wide tooltip
            : KoolDockSettings::tooltipSize() * 3 / 2 + 24) // horizontal: tall tooltip
        : 0;
    return qMax(bgHeight, needed + tooltipReserve);
}

int KoolDock::maxDockWidth() const
{
    // Window covers the full screen so it never resizes — no
    // buffer-stretch flicker.  Match the selected screen, falling back
    // to primary.
    auto *s = QGuiApplication::primaryScreen();
    const QString target = KoolDockSettings::screenName();
    for (auto *c : QGuiApplication::screens()) {
        if (c->name() == target) { s = c; break; }
    }
    return s ? s->size().width() : 1920;
}

int KoolDock::maxDockHeight() const
{
    auto *s = QGuiApplication::primaryScreen();
    const QString target = KoolDockSettings::screenName();
    for (auto *c : QGuiApplication::screens()) {
        if (c->name() == target) { s = c; break; }
    }
    return s ? s->size().height() : 1080;
}

static constexpr int TRIGGER_HEIGHT = 8;

void KoolDock::applyInputMask(bool hidden)
{
    if (!m_view) return;
    if (!hidden) {
        // Full input: clear any mask so the entire surface receives
        // pointer and touch events.
        m_view->setMask(QRegion());
        return;
    }
    const int w = maxDockWidth();
    const int h = maxDockHeight();
    const int bgHeight = KoolDockSettings::dockHeight();

    if (autoHide()) {
        // Autohide: restrict to an 8 px trigger strip at the anchored edge,
        // sized to the pill's unzoomed (rest) length so the dock only wakes
        // when the cursor is within the pill's horizontal/vertical footprint,
        // not the full screen edge. Using the rest length (not maxDockLongSize
        // which includes zoom expansion) matches the visible pill bounds
        // exactly — DockBar.qml's contentLength at rest equals
        // iconSpacing + count * (smallIconSize + iconSpacing).
        const int smallIconSize = KoolDockSettings::smallIconSize();
        const int iconSpacing = KoolDockSettings::iconSpacing();
        const int count = m_model ? m_model->count() : 0;
        const int restPillLen = count > 0
            ? iconSpacing + count * (smallIconSize + iconSpacing)
            : 64;
        QRect strip;
        switch (screenEdge()) {
        case Qt::BottomEdge:
            strip = QRect((w - restPillLen) / 2, h - TRIGGER_HEIGHT, restPillLen, TRIGGER_HEIGHT);
            break;
        case Qt::TopEdge:
            strip = QRect((w - restPillLen) / 2, 0, restPillLen, TRIGGER_HEIGHT);
            break;
        case Qt::LeftEdge:
            strip = QRect(0, (h - restPillLen) / 2, TRIGGER_HEIGHT, restPillLen);
            break;
        case Qt::RightEdge:
            strip = QRect(w - TRIGGER_HEIGHT, (h - restPillLen) / 2, TRIGGER_HEIGHT, restPillLen);
            break;
        }
        m_view->setMask(QRegion(strip));
    } else {
        // Non-autohide: restrict to the pill area so clicks on windows
        // behind the full-screen transparent overflow pass through.
        const int pillLen = maxDockLongSize();
        const bool vert = (screenEdge() == Qt::LeftEdge || screenEdge() == Qt::RightEdge);
        QRect rect;
        if (vert) {
            const qreal baseX = (screenEdge() == Qt::RightEdge) ? (w - bgHeight) : 0;
            const qreal centerY = (h - pillLen) / 2;
            rect = QRect(baseX, centerY, bgHeight, pillLen);
        } else {
            const qreal baseY = (screenEdge() == Qt::TopEdge) ? 0 : (h - bgHeight);
            const qreal centerX = (w - pillLen) / 2;
            rect = QRect(centerX, baseY, pillLen, bgHeight);
        }
        m_view->setMask(QRegion(rect));
    }
}

void KoolDock::applyBlur()
{
    if (!m_view) return;
    m_blurDirty = false;
    if (KoolDockSettings::blurBackground()) {
        // If QML hasn't pushed a blur region yet (m_blurLength == 0),
        // skip — the fallback to m_view->width() would blur the full
        // screen for a frame since the window is panel-sized.
        if (m_blurLength <= 0) {
            // Not initialized yet — retry so the blur eventually goes live.
            if (!m_blurTimer.isActive())
                m_blurTimer.start();
            return;
        }
        const qreal pos = m_blurPos;
        const qreal length = m_blurLength;
        const qreal shortOffset = m_blurShortOffset;
        const int bgHeight = KoolDockSettings::dockHeight();
        const bool vert = (screenEdge() == Qt::LeftEdge || screenEdge() == Qt::RightEdge);
        QRectF rect;
        if (vert) {
            const qreal baseX = (screenEdge() == Qt::RightEdge) ? (m_view->width() - bgHeight) : 0;
            rect = QRectF(baseX + shortOffset, pos, bgHeight, length);
        } else {
            const qreal baseY = (screenEdge() == Qt::TopEdge) ? 0 : (m_view->height() - bgHeight);
            rect = QRectF(pos, baseY + shortOffset, length, bgHeight);
        }
        // Use a rounded-rect path so the blur doesn't extend past the
        // pill's corners (which are transparent).  The QPainterPath →
        // QRegion chain can produce an empty region when integer
        // rounding collapses the curved-corner polygon to zero; fall
        // back to a plain aligned rect in that case so KWin never sees
        // an empty blur region (which it treats as "no blur").
        QPainterPath path;
        path.addRoundedRect(rect, m_blurRadius, m_blurRadius);
        QRegion region(path.toFillPolygon().toPolygon());
        if (region.isEmpty())
            region = QRegion(rect.toAlignedRect());
        KWindowEffects::enableBlurBehind(m_view, true, region);
    } else {
        KWindowEffects::enableBlurBehind(m_view, false);
    }
    // If QML pushed more updates while we were waiting for this timer
    // tick, re-arm so we catch up in the next interval.
    if (m_blurDirty)
        m_blurTimer.start();
}

void KoolDock::updateBlurRegion(qreal longPos, qreal longLength, qreal shortOffset, qreal radius)
{
    m_blurPos = longPos;
    m_blurLength = longLength;
    m_blurShortOffset = shortOffset;
    m_blurRadius = radius;
    m_blurDirty = true;
    if (!m_blurTimer.isActive())
        m_blurTimer.start();
}

void KoolDock::reconfigure()
{
    if (m_panelSurface) {
        applyPanelShell();
    }
    applySpacer();
    // Don't call applyBlur() directly — a direct call races with QML
    // property re-evaluation after applyPanelShell()'s window resize,
    // so m_blurPos/m_blurLength may be stale or zero (producing a
    // full-screen blur flash).  Arm the throttled timer instead; QML
    // will push fresh values via updateBlurRegion before it fires.
    m_blurDirty = true;
    if (!m_blurTimer.isActive())
        m_blurTimer.start();
    Q_EMIT screenEdgeChanged();
    Q_EMIT autoHideChanged();
    Q_EMIT themeNameChanged();
    m_model->reload();
}

void KoolDock::reload()
{
    reconfigure();
}

void KoolDock::showPreferences()
{
    // The dock is a full-screen panel surface sitting above normal
    // windows. Make it transparent to input while the dialog is open
    // so the user can interact with it. Restore when the dialog closes.
    m_view->setFlag(Qt::WindowTransparentForInput, true);

    auto *dialog = new QQuickView();
    // The dialog has its own QML engine, so it needs its own "kicon"
    // image provider — the one on m_view's engine isn't shared. Without
    // this, image://kicon/... sources (e.g. the app logo on the About
    // tab) silently fail to resolve and render nothing.
    dialog->engine()->addImageProvider(QStringLiteral("kicon"), new IconImageProvider());
    dialog->setFlag(Qt::Dialog);
    dialog->setFlag(Qt::WindowStaysOnTopHint);
    // SizeRootObjectToView: the window size drives the root item's
    // width/height, so QML layouts that fill the root adapt when the
    // user resizes the dialog.  SizeViewToRootObject (the opposite
    // mode) would let the content dictate the window size, ignoring
    // manual resizes.
    dialog->setResizeMode(QQuickView::SizeRootObjectToView);
    dialog->setMinimumSize(QSize(480, 360));
    dialog->resize(600, 520);
    dialog->setTitle(i18n("KoolDock Preferences"));
    dialog->rootContext()->setContextObject(new KLocalizedContext(dialog));
    dialog->rootContext()->setContextProperty(QStringLiteral("settings"), KoolDockSettings::self());
    dialog->rootContext()->setContextProperty(QStringLiteral("kooldock"), this);
    dialog->setSource(QUrl(QStringLiteral("qrc:/qml/SettingsDialog.qml")));

    QObject::connect(dialog, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible) {
            m_view->setFlag(Qt::WindowTransparentForInput, false);
        }
    });
    dialog->show();
}

void KoolDock::quit()
{
    QCoreApplication::quit();
}

void KoolDock::toggleOrientation()
{
    int o = KoolDockSettings::orientation();
    KoolDockSettings::setOrientation((o + 1) % 4);
    KoolDockSettings::self()->save();
    reconfigure();
}

void KoolDock::setScreenEdge(Qt::Edge edge)
{
    int o = 0;
    switch (edge) {
    case Qt::BottomEdge: o = 0; break;
    case Qt::LeftEdge:   o = 1; break;
    case Qt::TopEdge:    o = 2; break;
    case Qt::RightEdge:  o = 3; break;
    default: return;
    }
    KoolDockSettings::setOrientation(o);
    KoolDockSettings::self()->save();
    reconfigure();
}

void KoolDock::onScreenChanged(QScreen *screen)
{
    Q_UNUSED(screen);
    applyGeometry();
}

void KoolDock::setContainsMouse(bool contains)
{
    if (m_containsMouse == contains) {
        return;
    }
    m_containsMouse = contains;
    if (m_panelSurface && KoolDockSettings::autoHide()) {
        if (contains) {
            m_hideTimer.stop();
            applyInputMask(false);
        } else {
            m_hideTimer.start(200);
        }
    }
    // Non-autohide: toggle the input mask between full (hovering) and
    // pill-only (not hovering) so clicks pass through the transparent
    // overflow to windows behind the full-screen dock.
    if (m_panelSurface && !KoolDockSettings::autoHide()) {
        applyInputMask(!contains);
    }
    Q_EMIT containsMouseChanged();
}

void KoolDock::setTooltipExtent(int px)
{
    // Tooltip space is pre-reserved in maxDockShortSize(); the dynamic
    // resize was removed to eliminate buffer-stretch flicker.  Store the
    // value for potential future use but don't resize the window.
    m_tooltipExtent = px;
}

void KoolDock::setPointerDistanceFromEdge(qreal distance)
{
    m_pointerDistanceFromEdge = distance;
}

void KoolDock::onHideTimer()
{
    if (!m_panelSurface || !KoolDockSettings::autoHide() || m_containsMouse) {
        return;
    }
    // Restrict the pointer input region to the trigger strip so normal
    // mouse movement only wakes the dock at the screen edge.  The window
    // stays full-size — drag-and-drop from external apps sees the full
    // surface geometry and can enter to trigger expansion.
    applyInputMask(true);
    // Don't disable the blur here: the blur region is already animated
    // together with the pill via slideTransform.onYChanged →
    // applyBlur(), so by the end of the 200ms slide-out the blur rect
    // is off-screen.  Disabling it explicitly races the animation: if
    // the timer fires a frame before the slide finishes, the blur
    // disappears while the pill is still visible, producing a one-frame
    // "background vanishes" flicker every time the dock hides.
}

void KoolDock::showAppMenu()
{
    // Open the KDE application launcher (Kickoff).  plasmawindowed shows
    // the widget in a standalone window on Plasma 6; fall back to krunner
    // on systems where plasmawindowed isn't installed.
    QProcess::startDetached(QStringLiteral("plasmawindowed"),
        {QStringLiteral("org.kde.plasma.kickoff")});
}

void KoolDock::openTrash()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("trash:/")));
}

void KoolDock::trashFiles(const QVariantList &urls)
{
    QList<QUrl> urlList;
    urlList.reserve(urls.size());
    for (const QVariant &v : urls) {
        urlList.append(v.toUrl());
    }
    if (!urlList.isEmpty()) {
        KIO::trash(urlList);
    }
}

bool KoolDock::isTrashEmpty() const
{
    const QString trashFiles = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                               + QStringLiteral("/Trash/files");
    QDir dir(trashFiles);
    if (!dir.exists()) return true;
    return dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty();
}

void KoolDock::updateTrashState()
{
    const bool empty = isTrashEmpty();
    if (m_trashIsEmpty != empty) {
        m_trashIsEmpty = empty;
        Q_EMIT trashIsEmptyChanged();
    }
}

void KoolDock::emptyTrash()
{
    const QString trashRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                              + QStringLiteral("/Trash");
    QDir(trashRoot + QStringLiteral("/files")).removeRecursively();
    QDir(trashRoot + QStringLiteral("/info")).removeRecursively();
    // Recreate the directories so the trash can still receive files.
    QDir().mkpath(trashRoot + QStringLiteral("/files"));
    QDir().mkpath(trashRoot + QStringLiteral("/info"));
}

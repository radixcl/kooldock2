// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kooldock.h"

#include "dockmodel.h"
#include "kooldocksettings.h"
#include "version.h"
#include "windowactions.h"
#include "windowtasks.h"

#include <LayerShellQt/Window>

#include <KConfigDialog>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <KWindowEffects>
#include <KIO/CopyJob>

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QIcon>
#include <QPainterPath>
#include <QProcess>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <qnativeinterface.h>

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
    connect(&m_shrinkTimer, &QTimer::timeout, this, &KoolDock::applyLayerShell);

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

    m_tasks->start();

    setupView();

    connect(m_model, &DockModel::countChanged, this, [this]() { applyLayerShell(); });

    connect(m_model, &DockModel::activateAppMenu, this, &KoolDock::showAppMenu);
    connect(m_model, &DockModel::activateTrash, this, &KoolDock::openTrash);

    connect(KoolDockSettings::self(), &KCoreConfigSkeleton::configChanged,
            this, [this]() { reconfigure(); });
    refreshScreens();
    connect(qApp, &QGuiApplication::screenAdded,   this, [this](QScreen *) { refreshScreens(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *s) {
        refreshScreens();
        // If the configured screen was the one that disappeared, clear the
        // setting so the dock falls back to primary and re-apply the layer.
        if (KoolDockSettings::screenName() == s->name()) {
            KoolDockSettings::setScreenName(QString());
            KoolDockSettings::self()->save();
            Q_EMIT screenNameChanged();
            applyLayerShell();
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

    m_view->rootContext()->setContextObject(new KLocalizedContext(m_view));
    m_view->rootContext()->setContextProperty(QStringLiteral("settings"), KoolDockSettings::self());

    m_view->setSource(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    auto *root = m_view->rootObject();
    if (root) {
        root->setProperty("kooldock", QVariant::fromValue(this));
    }

    m_view->setResizeMode(QQuickView::SizeRootObjectToView);

    applyLayerShell();
    applyGeometry();
    // Skip blur on startup when auto-hiding: the window starts hidden
    // (a narrow trigger strip), so a blur region would show a blurred
    // rectangle at the edge with no visible pill. Blur is enabled by
    // setContainsMouse() when the dock expands.
    if (!autoHide()) {
        applyBlur();
    }

    m_view->show();
    if (!autoHide()) {
        QTimer::singleShot(100, this, [this]() { applyBlur(); });
    }
}

// Resize strategy: setDesiredSize() tells the compositor the desired
// surface size without forcing an immediate QWindow resize — QtWayland
// only commits the new geometry together with a matching buffer after
// acking the configure event, so no frame with a size-mismatched buffer
// is ever presented (no visible stretch).  On older LayerShellQt
// (< 6.6.4, e.g. Ubuntu 25.04) fall back to setMinimumSize/setMaximumSize;
// the one-frame stretch may appear on those distros.
void KoolDock::applyLayerShell()
{
    if (!m_view) {
        return;
    }

    m_layer = LayerShellQt::Window::get(m_view);
    if (!m_layer) {
        return;
    }

    using L = LayerShellQt::Window;
    // Anchor to two perpendicular edges so the compositor can't shift the
    // window away from the physical screen corner when other panels (e.g.
    // the KDE bottom panel) have their own exclusive zones. Without the
    // second anchor the compositor may slide the surface along the
    // perpendicular axis to avoid overlapping other panels' reserved space.
    L::Anchors anchors{L::AnchorNone};
    switch (screenEdge()) {
    case Qt::BottomEdge: anchors = L::AnchorBottom; break;
    case Qt::TopEdge:    anchors = L::AnchorTop; break;
    case Qt::LeftEdge:   anchors = L::AnchorLeft; break;
    case Qt::RightEdge:  anchors = L::AnchorRight; break;
    }
    // Lock the perpendicular axis: horizontal docks (top/bottom) lock to
    // the left edge; vertical docks (left/right) lock to the top edge.
    if (screenEdge() == Qt::LeftEdge || screenEdge() == Qt::RightEdge)
        anchors |= L::AnchorTop;
    else
        anchors |= L::AnchorLeft;
    m_layer->setAnchors(anchors);
    m_layer->setLayer(L::LayerTop);
    m_layer->setKeyboardInteractivity(L::KeyboardInteractivityOnDemand);
    m_layer->setScope(QStringLiteral("kooldock2"));

    // Screen selection — place the layer surface on the configured
    // monitor, falling back to the primary screen if none is set or
    // the configured name doesn't match any connected screen.
    {
        const QString target = KoolDockSettings::screenName();
        QScreen *chosen = QGuiApplication::primaryScreen();
        for (auto *s : QGuiApplication::screens()) {
            if (s->name() == target) {
                chosen = s;
                break;
            }
        }
#ifdef LAYERSHELLQT_HAS_SET_SCREEN
        m_layer->setScreen(chosen);
#else
        m_view->setScreen(chosen);
#endif
    }

    // Edge margin: a negative value pushes the dock past other panels
    // (e.g. the KDE taskbar) toward the screen edge, so the dock can sit
    // below the taskbar instead of above it. Applied as a layer-shell
    // margin on the anchored edge only.
    const int edgeMargin = KoolDockSettings::edgeMargin();
    QMargins margins;
    switch (screenEdge()) {
    case Qt::BottomEdge: margins.setBottom(edgeMargin); break;
    case Qt::TopEdge:    margins.setTop(edgeMargin); break;
    case Qt::LeftEdge:   margins.setLeft(edgeMargin); break;
    case Qt::RightEdge:  margins.setRight(edgeMargin); break;
    }
    m_layer->setMargins(margins);

    const QSize size(maxDockWidth(), maxDockHeight());
    if (m_debugBounds) qDebug() << "applyLayerShell: edge=" << screenEdge()
        << "screen=" << (m_view->screen() ? m_view->screen()->name() : QStringLiteral("null"))
        << "screenSize=" << (m_view->screen() ? m_view->screen()->size() : QSize())
        << "desiredSize=" << size
        << "edgeMargin=" << edgeMargin
        << "exclusiveZone=0";

    // Window is always full-screen — the pill is positioned at the anchored
    // edge by QML and the rest is transparent. Setting exclusive zone to 0
    // prevents the compositor from squeezing the window when another panel
    // (e.g. the KDE taskbar) has its own exclusive zone at the same edge,
    // which would make parent.width/height in QML ≠ screen size and
    // throw the pill off-center. The dock is at LayerTop so it renders
    // above other surfaces regardless of exclusive zone.
#ifdef LAYERSHELLQT_HAS_SET_DESIRED_SIZE
    m_layer->setDesiredSize(size);
#else
    m_view->setMinimumSize(size);
    m_view->setMaximumSize(size);
#endif
    m_layer->setExclusiveZone(0);

    applyInputMask(!m_containsMouse);
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
        // Autohide: restrict to an 8 px trigger strip at the anchored edge.
        QRect strip;
        switch (screenEdge()) {
        case Qt::BottomEdge: strip = QRect(0, h - TRIGGER_HEIGHT, w, TRIGGER_HEIGHT); break;
        case Qt::TopEdge:    strip = QRect(0, 0, w, TRIGGER_HEIGHT); break;
        case Qt::LeftEdge:   strip = QRect(0, 0, TRIGGER_HEIGHT, h); break;
        case Qt::RightEdge:  strip = QRect(w - TRIGGER_HEIGHT, 0, TRIGGER_HEIGHT, h); break;
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
        const qreal pos = m_blurPos;
        const qreal length = m_blurLength > 0 ? m_blurLength : m_view->width();
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
    if (m_layer) {
        applyLayerShell();
    }
    applyBlur();
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
    // The dock is a full-screen LayerTop surface sitting above normal
    // windows. Make it transparent to input while the dialog is open
    // so the user can interact with it. Restore when the dialog closes.
    m_view->setFlag(Qt::WindowTransparentForInput, true);

    auto *dialog = new QQuickView();
    dialog->setFlag(Qt::Dialog);
    dialog->setFlag(Qt::WindowStaysOnTopHint);
    dialog->setResizeMode(QQuickView::SizeViewToRootObject);
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
    if (m_layer && KoolDockSettings::autoHide()) {
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
    if (m_layer && !KoolDockSettings::autoHide()) {
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
    if (!m_layer || !KoolDockSettings::autoHide() || m_containsMouse) {
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

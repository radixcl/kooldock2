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

class IconImageProvider : public QQuickImageProvider
{
public:
    IconImageProvider() : QQuickImageProvider(Pixmap) {}
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        QIcon icon = QIcon::fromTheme(id);
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
    , m_model(new DockModel(m_tasks, this))
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
    // Re-apply the layer shell with the new (expanded or normal) size.
    // The expansion grows the window along its short axis (perpendicular
    // to the screen edge) so the cursor stays inside the Wayland surface
    // while an icon is dragged outside the dock's visible area — without
    // it, the DragHandler stops tracking at the surface edge and the icon
    // appears "stuck" at the invisible window border.
    applyLayerShell();
}
QString KoolDock::version() const { return QString::fromLatin1(KOOLDOCK_VERSION); }
QString KoolDock::themeName() const { return KoolDockSettings::themeName(); }

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
    L::Anchors anchors;
    switch (screenEdge()) {
    case Qt::BottomEdge: anchors = L::AnchorBottom; break;
    case Qt::TopEdge:    anchors = L::AnchorTop; break;
    case Qt::LeftEdge:   anchors = L::AnchorLeft; break;
    case Qt::RightEdge:  anchors = L::AnchorRight; break;
    }
    m_layer->setAnchors(anchors);
    m_layer->setLayer(L::LayerTop);
    m_layer->setKeyboardInteractivity(L::KeyboardInteractivityOnDemand);
    m_layer->setScope(QStringLiteral("kooldock2"));

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

    const int bgHeight = KoolDockSettings::dockHeight();
    const bool overlay = autoHide() || edgeMargin != 0;
    QSize size(maxDockWidth(), maxDockHeight());
    // During an internal icon drag, expand the window along its short
    // axis so the DragHandler keeps tracking the cursor as the icon moves
    // outside the dock's visible area.
    if (m_dragExpanded) {
        constexpr int dragExpandShort = 300;
        const bool vert = (screenEdge() == Qt::LeftEdge || screenEdge() == Qt::RightEdge);
        if (vert) {
            size.rwidth() += dragExpandShort;
        } else {
            size.rheight() += dragExpandShort;
        }
    }
    // Tooltip space is pre-reserved in maxDockShortSize() for all edges
    // — no dynamic grow is needed here.  Eliminating the per-tooltip
    // resize removes the one-frame buffer-stretch flicker the compositor
    // produces every time the surface size changes.
    //
    // Non-autohide and no zoom active: shrink the window's short axis to
    // just bgHeight so the transparent overflow area doesn't intercept
    // clicks on windows above/behind the dock. When the cursor enters the
    // pill zone, setContainsMouse() re-expands to the full max size. This
    // only applies when no drag is active (drag operations always need the
    // full surface).
    if (!autoHide() && !m_dragExpanded && !m_containsMouse && !m_dragActive) {
        const bool vert = (screenEdge() == Qt::LeftEdge || screenEdge() == Qt::RightEdge);
        if (vert) {
            size.setWidth(bgHeight);
        } else {
            size.setHeight(bgHeight);
        }
    }
#ifdef LAYERSHELLQT_HAS_SET_DESIRED_SIZE
    m_layer->setDesiredSize(size);
#else
    m_view->setMinimumSize(size);
    m_view->setMaximumSize(size);
#endif
    m_layer->setExclusiveZone(overlay ? 0 : bgHeight);
    m_layer->setExclusiveEdge(static_cast<L::Anchor>(0));
    if (!overlay) {
        switch (screenEdge()) {
        case Qt::BottomEdge: m_layer->setExclusiveEdge(L::AnchorBottom); break;
        case Qt::TopEdge:    m_layer->setExclusiveEdge(L::AnchorTop); break;
        case Qt::LeftEdge:   m_layer->setExclusiveEdge(L::AnchorLeft); break;
        case Qt::RightEdge:  m_layer->setExclusiveEdge(L::AnchorRight); break;
        }
    }

    if (autoHide()) {
        applyInputMask(!m_containsMouse);
    }
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
    // Window width = long axis on horizontal edges, short axis on vertical.
    const bool vert = (screenEdge() == Qt::LeftEdge || screenEdge() == Qt::RightEdge);
    return vert ? maxDockShortSize() : maxDockLongSize();
}

int KoolDock::maxDockHeight() const
{
    // Window height = short axis on horizontal edges, long axis on vertical.
    const bool vert = (screenEdge() == Qt::LeftEdge || screenEdge() == Qt::RightEdge);
    return vert ? maxDockLongSize() : maxDockShortSize();
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
    // Restrict input to the trigger strip at the anchored edge.  On
    // Wayland QWindow::setMask translates to wl_surface::set_input_region,
    // which only affects pointer/touch hit-testing — the surface geometry
    // (used for wl_data_device::enter during drag-and-drop) stays at the
    // full window size, so drags from external apps can still wake the
    // dock.
    //
    // Use maxDockWidth()/maxDockHeight() (the *desired* size for the
    // current orientation) rather than m_view->width()/height() (the
    // *current* QWindow size). When switching edges (e.g. Bottom → Left),
    // setDesiredSize has been called but the compositor hasn't yet
    // configured the new surface size; the trigger strip must match the
    // eventual window size, not the stale one, or the strip will only
    // cover a fraction of the edge — the uncovered area never receives
    // pointer-enter and the dock can't wake up, requiring a restart.
    const int w = maxDockWidth();
    const int h = maxDockHeight();
    QRect strip;
    switch (screenEdge()) {
    case Qt::BottomEdge: strip = QRect(0, h - TRIGGER_HEIGHT, w, TRIGGER_HEIGHT); break;
    case Qt::TopEdge:    strip = QRect(0, 0, w, TRIGGER_HEIGHT); break;
    case Qt::LeftEdge:   strip = QRect(0, 0, TRIGGER_HEIGHT, h); break;
    case Qt::RightEdge:  strip = QRect(w - TRIGGER_HEIGHT, 0, TRIGGER_HEIGHT, h); break;
    }
    m_view->setMask(QRegion(strip));
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
    auto *dialog = new QQuickView();
    dialog->setFlag(Qt::Dialog);
    // Unlike the main dock view, this is a plain desktop window with no
    // layer-shell surface dictating its size, so let it size itself to the
    // QML content's implicit size instead of the other way around — with
    // SizeRootObjectToView and no explicit resize(), the window opened at
    // Qt's tiny platform-default size and the content never fit.
    dialog->setResizeMode(QQuickView::SizeViewToRootObject);
    dialog->setTitle(i18n("KoolDock Preferences"));
    dialog->rootContext()->setContextObject(new KLocalizedContext(dialog));
    dialog->rootContext()->setContextProperty(QStringLiteral("settings"), KoolDockSettings::self());
    dialog->rootContext()->setContextProperty(QStringLiteral("kooldock"), this);
    dialog->setSource(QUrl(QStringLiteral("qrc:/qml/SettingsDialog.qml")));
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
    // Non-autohide: resize the window to just the pill height when the
    // cursor is outside, and to the full size (with zoom overflow room)
    // when the cursor enters. This keeps the transparent overflow area
    // from intercepting clicks on windows above the dock.
    //
    // Growing happens immediately, so the icons have room to zoom into.
    // Shrinking is delayed by zoomSpeed instead: the Wayland surface
    // resize is an instant hard cut that no QML Behavior can animate, so
    // shrinking right away would clip the icons mid zoom-out — they
    // haven't visually shrunk back to restingSize yet. Waiting lets that
    // animation finish first.
    if (m_layer && !KoolDockSettings::autoHide() && !m_dragExpanded && !m_dragActive) {
        if (contains) {
            m_shrinkTimer.stop();
            applyLayerShell();
        } else {
            m_shrinkTimer.start(KoolDockSettings::zoomSpeed());
        }
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

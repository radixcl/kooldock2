// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kooldock.h"

#include "dockmodel.h"
#include "kooldocksettings.h"
#include "windowactions.h"
#include "windowtasks.h"

#include <LayerShellQt/Window>

#include <KConfigDialog>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <KWindowEffects>

#include <QIcon>
#include <QPainterPath>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QScreen>
#include <QTimer>

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

void KoolDock::create(QObject *parent, bool showPreferences)
{
    Q_ASSERT(!s_instance);
    s_instance = new KoolDock(parent);
    if (showPreferences) {
        QTimer::singleShot(0, s_instance, &KoolDock::showPreferences);
    }
}

KoolDock::KoolDock(QObject *parent)
    : QObject(parent)
    , m_tasks(new WindowTasks(this))
    , m_model(new DockModel(m_tasks, this))
    , m_windowActions(new WindowActions(m_tasks, this))
    , m_config(KSharedConfig::openConfig(QStringLiteral("kooldockrc")))
{
    connect(m_tasks, &WindowTasks::windowAdded, m_model, &DockModel::onWindowAdded);
    connect(m_tasks, &WindowTasks::windowRemoved, m_model, &DockModel::onWindowRemoved);
    connect(m_tasks, &WindowTasks::windowChanged, m_model, &DockModel::onWindowChanged);
    connect(m_tasks, &WindowTasks::activeWindowChanged, m_model, &DockModel::onActiveWindowChanged);
    connect(m_tasks, &WindowTasks::availabilityChanged, m_model, &DockModel::reload);

    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, &KoolDock::onHideTimer);

    m_dragHeartbeat.setSingleShot(true);
    connect(&m_dragHeartbeat, &QTimer::timeout, this, [this]() {
        setDragActive(false);
    });

    m_tasks->start();

    setupView();

    connect(m_model, &DockModel::countChanged, this, [this]() { applyLayerShell(); });

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

void KoolDock::setDragActive(bool active)
{
    if (active) {
        // Keep the heartbeat alive while drag events flow.
        m_dragHeartbeat.start(500);
    }
    if (m_dragActive == active) return;
    m_dragActive = active;
    Q_EMIT dragActiveChanged();
    setContainsMouse(active);
}
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
    // When auto-hide is on, the window stays at full size (so drag-drop
    // from external apps can hit the surface) but we restrict pointer
    // input to a narrow trigger strip via setMask / input region.
    // With an edge margin set (non-zero), use overlay mode (exclusive
    // zone 0) so the dock doesn't reserve extra space on top of the
    // taskbar's — it just overlays at the offset position.
    const bool overlay = autoHide() || edgeMargin != 0;
    const QSize size(maxDockWidth(), maxDockHeight());
    m_layer->setDesiredSize(size);
    if (m_view) m_view->resize(size);
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
    // possible icon plus its margin. Same for all four edges.
    const int bgHeight = KoolDockSettings::dockHeight();
    const int needed = KoolDockSettings::iconSpacing() + KoolDockSettings::bigIconSize() + 4;
    int shortSize = qMax(bgHeight, needed);
    // Reserve space above the tallest zoomed icon for the in-scene tooltip
    // (DockItem.qml renders it in the overflow area, away from the screen
    // edge). Without this, Wayland clips the tooltip to the surface edge.
    // The tooltip is ~24px tall (12px text + padding) with an 8px gap; 40
    // gives a comfortable margin. Only needed when tooltips are enabled.
    if (KoolDockSettings::showNames()) {
        shortSize += 40;
    }
    return shortSize;
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
    const int w = m_view->width();
    const int h = m_view->height();
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
    if (KoolDockSettings::blurBackground()) {
        // Blur only the background pill's actual current bounds, kept in
        // sync with the QML side via updateBlurRegion() as it resizes with
        // the zoom — not the wider reserved overflow area, which must stay
        // transparent. QML reports the pill's position and length along
        // the dock's long axis (pos/length) plus the short-axis offset
        // (shortOffset — the slide transform's displacement during the
        // auto-hide animation); we reconstruct the full rect from the edge
        // orientation. On horizontal edges the long axis is x and the
        // short axis (bgHeight) is y; on vertical edges they swap. The
        // region is rounded to match the pill's radius; a plain rectangular
        // region would blur the four corners that the rounded rectangle
        // leaves transparent. Don't clamp pos to 0: during the auto-hide
        // slide the pill (and its blur) move off-screen, and clamping would
        // keep a blurred rectangle pinned at the edge.
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
        QPainterPath path;
        path.addRoundedRect(rect, m_blurRadius, m_blurRadius);
        KWindowEffects::enableBlurBehind(m_view, true, QRegion(path.toFillPolygon().toPolygon()));
    } else {
        KWindowEffects::enableBlurBehind(m_view, false);
    }
}

void KoolDock::updateBlurRegion(qreal longPos, qreal longLength, qreal shortOffset, qreal radius)
{
    m_blurPos = longPos;
    m_blurLength = longLength;
    m_blurShortOffset = shortOffset;
    m_blurRadius = radius;
    applyBlur();
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
    // macOS-style auto-hide: the dock overlays on top of other windows
    // (exclusive zone stays 0 — never pushes them) and slides in/out from
    // the screen edge with a fade. The window is always full-size; when
    // hidden the pointer input region is restricted to the trigger strip
    // (so normal mouse movement doesn't wake the dock except at the edge)
    // while drag-and-drop sees the full surface geometry. When the cursor
    // bumps the edge, the QML pill animates in (slide + fade). When the
    // cursor leaves, the QML pill animates out, and after the animation
    // finishes onHideTimer() restricts the input region again and disables
    // blur.
    if (m_layer && KoolDockSettings::autoHide()) {
        if (contains) {
            // Show: cancel any pending hide, open full input region.
            // Don't call applyBlur() here — QML's updateBlur() fires
            // immediately (and on every frame of the slide animation)
            // passing the correct transform-aware position.
            m_hideTimer.stop();
            applyInputMask(false);
        } else {
            // Hide: start the timer — the QML animation plays during this
            // delay, then onHideTimer() restricts input and disables blur.
            m_hideTimer.start(200);
        }
    }
    Q_EMIT containsMouseChanged();
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
    KWindowEffects::enableBlurBehind(m_view, false);
}

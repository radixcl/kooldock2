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
    applyBlur();

    m_view->show();
    QTimer::singleShot(100, this, [this]() { applyBlur(); });
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

    const int bgHeight = 60;
    m_layer->setDesiredSize(QSize(maxDockWidth(), maxDockHeight()));
    m_layer->setExclusiveZone(autoHide() ? 0 : bgHeight);
    m_layer->setExclusiveEdge(static_cast<L::Anchor>(0));
    if (!autoHide()) {
        switch (screenEdge()) {
        case Qt::BottomEdge: m_layer->setExclusiveEdge(L::AnchorBottom); break;
        case Qt::TopEdge:    m_layer->setExclusiveEdge(L::AnchorTop); break;
        case Qt::LeftEdge:   m_layer->setExclusiveEdge(L::AnchorLeft); break;
        case Qt::RightEdge:  m_layer->setExclusiveEdge(L::AnchorRight); break;
        }
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

int KoolDock::maxDockWidth() const
{
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

    // Worst case: the cursor parked exactly on one icon, summing the
    // parabola bump for that icon and every neighbour still inside the
    // falloff radius (mirrors DockBar.qml's layout() math).
    int extraWidth = 0;
    for (int dx = 0; dx < W; dx += iDist) {
        const int bump = qMax(0, bigIconSize - (dx * dx * H) / (W * W) - smallIconSize);
        extraWidth += (dx == 0) ? bump : bump * 2;
    }

    const int restWidth = iconSpacing + count * iDist;
    return restWidth + extraWidth + iconSpacing * 2;
}

int KoolDock::maxDockHeight() const
{
    // The visible pill (Main.qml's bg) is always exactly bgHeight tall and
    // sits against the screen edge (bottom on BottomEdge, top on TopEdge);
    // icons grow away from that edge into the reserved space on the opposite
    // side. So the window needs enough room on that side to fit the tallest
    // possible icon plus its margin (matches Main.qml's iconSpacing-based
    // margin on DockBar) — independent of bgHeight and the same for both
    // horizontal edges.
    const int bgHeight = 60;
    const int needed = KoolDockSettings::iconSpacing() + KoolDockSettings::bigIconSize() + 4;
    return qMax(bgHeight, needed);
}

void KoolDock::applyBlur()
{
    if (!m_view) return;
    if (KoolDockSettings::blurBackground()) {
        // Blur only the background pill's actual current bounds (the 60px
        // band at the screen-edge side of the window), kept in sync with
        // the QML side via updateBlurRegion() as it resizes with the zoom —
        // not the wider reserved overflow area, which must stay transparent
        // just like the space on the opposite side of the bar. On
        // BottomEdge the pill sits at the window's bottom (y = h - bgHeight);
        // on TopEdge it sits at the top (y = 0). The region is rounded to
        // match the pill's radius; a plain rectangular region would blur the
        // four corners that the rounded rectangle actually leaves
        // transparent, showing a blurred square peeking out from behind the
        // rounded glass shape.
        const qreal x = qMax<qreal>(0, m_blurX);
        const qreal width = m_blurWidth > 0 ? m_blurWidth : m_view->width();
        const int bgHeight = 60;
        const qreal y = (screenEdge() == Qt::TopEdge) ? 0 : (m_view->height() - bgHeight);
        QPainterPath path;
        path.addRoundedRect(QRectF(x, y, width, bgHeight), m_blurRadius, m_blurRadius);
        KWindowEffects::enableBlurBehind(m_view, true, QRegion(path.toFillPolygon().toPolygon()));
    } else {
        KWindowEffects::enableBlurBehind(m_view, false);
    }
}

void KoolDock::updateBlurRegion(qreal x, qreal width, qreal radius)
{
    m_blurX = x;
    m_blurWidth = width;
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
    if (m_layer && KoolDockSettings::autoHide()) {
        if (contains) {
            m_layer->setExclusiveZone(60);
        } else {
            m_layer->setExclusiveZone(0);
        }
    }
    Q_EMIT containsMouseChanged();
}

#include "widgets.h"

#include <LayerShellQt/window.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QFontDatabase>
#include <QLocale>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QWindow>

namespace {

const char *const kMprisPrefix = "org.mpris.MediaPlayer2.";
const char *const kMprisPath = "/org/mpris/MediaPlayer2";
const char *const kPlayerInterface = "org.mpris.MediaPlayer2.Player";

// A dark outline under light text keeps it readable on any wallpaper.
void drawText(QPainter &p, const QRectF &rect, int flags, const QString &text, const QColor &color) {
    p.setPen(QColor(0, 0, 0, 110));
    p.drawText(rect.translated(0, 2), flags, text);
    p.setPen(color);
    p.drawText(rect, flags, text);
}

} // namespace

// --- MediaPlayers -------------------------------------------------------------

MediaPlayers::MediaPlayers(QObject *parent) : QObject(parent) {
    connect(&timer_, &QTimer::timeout, this, &MediaPlayers::poll);
    timer_.start(1000);
    poll();
}

QString MediaPlayers::pickPlayer() const {
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus) return QString();
    QStringList players;
    for (const QString &name : bus->registeredServiceNames().value())
        if (name.startsWith(kMprisPrefix)) players << name;
    if (players.isEmpty()) return QString();
    // Spotify first; otherwise whatever is playing; otherwise the first.
    for (const QString &name : players)
        if (name.mid(int(qstrlen(kMprisPrefix))).startsWith("spotify")) return name;
    for (const QString &name : players) {
        QDBusInterface props(name, kMprisPath, "org.freedesktop.DBus.Properties");
        QDBusReply<QDBusVariant> status = props.call("Get", kPlayerInterface, "PlaybackStatus");
        if (status.isValid() && status.value().variant().toString() == "Playing") return name;
    }
    return players.first();
}

void MediaPlayers::poll() {
    QString service = pickPlayer();
    if (service.isEmpty()) {
        if (!service_.isEmpty()) {
            service_.clear();
            emit changed();
        }
        return;
    }
    QDBusInterface props(service, kMprisPath, "org.freedesktop.DBus.Properties");
    props.setTimeout(500);
    QDBusReply<QVariantMap> reply = props.call("GetAll", kPlayerInterface);
    if (!reply.isValid()) return;
    QVariantMap all = reply.value();
    QVariantMap meta = qdbus_cast<QVariantMap>(all.value("Metadata"));

    service_ = service;
    QString title = meta.value("xesam:title").toString();
    QStringList artists = meta.value("xesam:artist").toStringList();
    QString artUrl = meta.value("mpris:artUrl").toString();
    bool trackChanged = title != title_;
    title_ = title;
    artist_ = artists.join(", ");
    playing_ = all.value("PlaybackStatus").toString() == "Playing";
    lengthUs_ = meta.value("mpris:length").toLongLong();

    // Spotify often reports a stale position; keep counting ourselves and
    // take the player's value whenever it actually moves.
    qint64 reported = all.value("Position").toLongLong();
    if (trackChanged)
        positionUs_ = reported;
    else if (reported != lastReportedUs_)
        positionUs_ = reported;
    else if (playing_)
        positionUs_ += 1000000;
    lastReportedUs_ = reported;
    if (lengthUs_ > 0) positionUs_ = qBound<qint64>(0, positionUs_, lengthUs_);

    if (artUrl != artUrl_) {
        artUrl_ = artUrl;
        art_ = QImage();
        QUrl url(artUrl);
        if (url.isLocalFile()) {
            art_.load(url.toLocalFile());
        } else if (url.scheme().startsWith("http")) {
            QNetworkReply *download = net_.get(QNetworkRequest(url));
            connect(download, &QNetworkReply::finished, this, [this, download, artUrl] {
                download->deleteLater();
                if (artUrl == artUrl_) art_.loadFromData(download->readAll());
                emit changed();
            });
        }
    }
    emit changed();
}

void MediaPlayers::call(const QString &method) {
    if (service_.isEmpty()) return;
    QDBusInterface player(service_, kMprisPath, kPlayerInterface);
    player.asyncCall(method);
    QTimer::singleShot(300, this, &MediaPlayers::poll);
}

// --- DesktopWidget ------------------------------------------------------------

DesktopWidget::DesktopWidget(QScreen *screen, Place place, bool clickable) {
    setAttribute(Qt::WA_TranslucentBackground);
    if (!clickable) setWindowFlag(Qt::WindowTransparentForInput);
    setScreen(screen);
    winId();

    using LS = LayerShellQt::Window;
    auto *layer = LS::get(windowHandle());
    // The same layer and kind as the wallpaper; made after it, so on top.
    layer->setLayer(LS::LayerBottom);
    layer->setScope("desktop");
    layer->setKeyboardInteractivity(LS::KeyboardInteractivityNone);
    layer->setExclusiveZone(0);
    LS::Anchors anchors;
    int column = int(place) % 3, row = int(place) / 3;
    if (column == 0) anchors |= LS::AnchorLeft;
    if (column == 2) anchors |= LS::AnchorRight;
    if (row == 0) anchors |= LS::AnchorTop;
    if (row == 2) anchors |= LS::AnchorBottom;
    // Anchored to nothing on an axis means centred on it.
    layer->setAnchors(anchors);
    // Clear of the screen edges and of a bottom panel.
    layer->setMargins(QMargins(48, 48, 48, 72));
}

// --- ClockWidget ----------------------------------------------------------------

ClockWidget::ClockWidget(QScreen *screen, const Settings &settings)
    : DesktopWidget(screen, settings.clockPlace, false), settings_(settings), scale_(settings.clockSize / 100.0) {
    setFixedSize(int(1000 * scale_), int(300 * scale_));
    connect(&tick_, &QTimer::timeout, this, [this] { update(); });
    tick_.start(settings_.clockSeconds ? 1000 : 5000);
}

QFont ClockWidget::clockFont(double pixelSize, int weight) const {
    QString family = settings_.clockFont;
    if (family.isEmpty()) {
        // Mond uses the Anurati font; otherwise a thin, wide sans.
        QStringList families = QFontDatabase::families();
        for (const char *candidate : {"Anurati", "Montserrat", "Raleway", "Quicksand", "Noto Sans", "DejaVu Sans"})
            if (families.contains(candidate)) {
                family = candidate;
                break;
            }
    }
    QFont f(family);
    f.setPixelSize(int(pixelSize));
    f.setWeight(QFont::Weight(weight));
    return f;
}

void ClockWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QColor color(settings_.clockColor);
    QDateTime now = QDateTime::currentDateTime();
    QLocale english(QLocale::English);

    // M O N D A Y  /  26 OCTOBER, 2026  /  - 20:45 -
    QFont day = clockFont(110 * scale_, QFont::Light);
    day.setLetterSpacing(QFont::AbsoluteSpacing, 32 * scale_);
    QFont small = clockFont(26 * scale_, QFont::Normal);
    small.setLetterSpacing(QFont::AbsoluteSpacing, 6 * scale_);

    QString time = english.toString(now.time(), settings_.clock24h ? (settings_.clockSeconds ? "HH:mm:ss" : "HH:mm")
                                                                   : (settings_.clockSeconds ? "h:mm:ss AP" : "h:mm AP"));
    QRectF area = rect();
    double dayHeight = 140 * scale_, lineHeight = 44 * scale_;
    double top = (area.height() - dayHeight - 2 * lineHeight) / 2;
    p.setFont(day);
    drawText(p, QRectF(0, top, area.width(), dayHeight), Qt::AlignCenter, english.toString(now, "dddd").toUpper(), color);
    p.setFont(small);
    drawText(p, QRectF(0, top + dayHeight, area.width(), lineHeight), Qt::AlignCenter,
             english.toString(now, "d MMMM, yyyy").toUpper(), color);
    drawText(p, QRectF(0, top + dayHeight + lineHeight, area.width(), lineHeight), Qt::AlignCenter,
             "- " + time + " -", color);
}

// --- MusicWidget ----------------------------------------------------------------

MusicWidget::MusicWidget(QScreen *screen, const Settings &settings, MediaPlayers *players)
    : DesktopWidget(screen, settings.musicPlace, true), settings_(settings), players_(players),
      scale_(settings.musicSize / 100.0) {
    setFixedSize(int(420 * scale_), int(132 * scale_));
    connect(players_, &MediaPlayers::changed, this, [this] {
        // Nothing playing: nothing shown.
        setVisible(players_->active() && !players_->title().isEmpty());
        update();
    });
    setVisible(players_->active() && !players_->title().isEmpty());
}

QRectF MusicWidget::button(int index) const {
    double size = 30 * scale_, gap = 14 * scale_;
    double left = 132 * scale_ + index * (size + gap);
    return QRectF(left, height() - size - 14 * scale_, size, size);
}

void MusicWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor color(settings_.musicColor);
    double s = scale_;

    // Rounded, dark, see-through card.
    QPainterPath card;
    card.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 18 * s, 18 * s);
    p.fillPath(card, QColor(17, 17, 27, 170));

    QRectF art(14 * s, 14 * s, 104 * s, 104 * s);
    QPainterPath artClip;
    artClip.addRoundedRect(art, 12 * s, 12 * s);
    if (!players_->art().isNull()) {
        p.save();
        p.setClipPath(artClip);
        p.drawImage(art, players_->art().scaled(art.size().toSize(), Qt::KeepAspectRatioByExpanding,
                                                Qt::SmoothTransformation));
        p.restore();
    } else {
        p.fillPath(artClip, QColor(255, 255, 255, 30));
    }

    double left = 132 * s, textWidth = width() - left - 16 * s;
    QFont title = font();
    title.setPixelSize(int(18 * s));
    title.setBold(true);
    p.setFont(title);
    p.setPen(color);
    p.drawText(QRectF(left, 14 * s, textWidth, 24 * s), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(title).elidedText(players_->title(), Qt::ElideRight, int(textWidth)));
    QFont artist = font();
    artist.setPixelSize(int(14 * s));
    p.setFont(artist);
    QColor dim = color;
    dim.setAlphaF(0.75f);
    p.setPen(dim);
    p.drawText(QRectF(left, 40 * s, textWidth, 20 * s), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(artist).elidedText(players_->artist(), Qt::ElideRight, int(textWidth)));

    // Progress bar.
    QRectF bar(left, 68 * s, textWidth, 4 * s);
    p.fillRect(bar, QColor(255, 255, 255, 50));
    if (players_->lengthUs() > 0) {
        double done = double(players_->positionUs()) / double(players_->lengthUs());
        p.fillRect(QRectF(bar.left(), bar.top(), bar.width() * qBound(0.0, done, 1.0), bar.height()), color);
    }

    // Previous, play/pause, next.
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    for (int i = 0; i < 3; ++i) {
        QRectF b = button(i);
        QPointF c = b.center();
        double r = b.width() / 2;
        if (i == 1) {
            p.setBrush(QColor(255, 255, 255, 40));
            p.drawEllipse(c, r, r);
            p.setBrush(color);
            if (players_->playing()) {
                p.drawRect(QRectF(c.x() - r * 0.4, c.y() - r * 0.45, r * 0.28, r * 0.9));
                p.drawRect(QRectF(c.x() + r * 0.12, c.y() - r * 0.45, r * 0.28, r * 0.9));
            } else {
                QPolygonF play{{c.x() - r * 0.3, c.y() - r * 0.5}, {c.x() + r * 0.5, c.y()}, {c.x() - r * 0.3, c.y() + r * 0.5}};
                p.drawPolygon(play);
            }
        } else {
            double dir = i == 0 ? -1 : 1;
            QPolygonF arrow{{c.x() - dir * r * 0.4, c.y() - r * 0.45}, {c.x() + dir * r * 0.3, c.y()},
                            {c.x() - dir * r * 0.4, c.y() + r * 0.45}};
            p.drawPolygon(arrow);
            p.drawRect(QRectF(c.x() + dir * r * 0.3 - (dir > 0 ? 0 : r * 0.15), c.y() - r * 0.45, r * 0.15, r * 0.9));
        }
    }
}

void MusicWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    if (button(0).contains(event->position())) players_->previous();
    if (button(1).contains(event->position())) players_->playPause();
    if (button(2).contains(event->position())) players_->next();
}

#include "widgets.h"

#include <LayerShellQt/window.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusObjectPath>
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
const QColor kAccent(0x1d, 0xb9, 0x54); // Spotify green, for shuffle/repeat when on

// Clear of the screen edges and of a bottom panel.
const int kEdge = 48, kBottomEdge = 72;

// A dark outline under light text keeps it readable on any wallpaper.
void drawText(QPainter &p, const QRectF &rect, int flags, const QString &text, const QColor &color) {
    p.setPen(QColor(0, 0, 0, 110));
    p.drawText(rect.translated(0, 2), flags, text);
    p.setPen(color);
    p.drawText(rect, flags, text);
}

QString clockTime(qint64 us) {
    qint64 seconds = qMax<qint64>(0, us / 1000000);
    return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
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
    QString artUrl = meta.value("mpris:artUrl").toString();
    QVariant trackId = meta.value("mpris:trackid");
    trackId_ = trackId.canConvert<QDBusObjectPath>() ? trackId.value<QDBusObjectPath>().path() : trackId.toString();
    bool trackChanged = title != title_;
    title_ = title;
    artist_ = meta.value("xesam:artist").toStringList().join(", ");
    playing_ = all.value("PlaybackStatus").toString() == "Playing";
    shuffle_ = all.value("Shuffle").toBool();
    QString loop = all.value("LoopStatus").toString();
    loop_ = loop == "Track" ? Loop::Track : loop == "Playlist" ? Loop::Playlist : Loop::None;
    lengthUs_ = meta.value("mpris:length").toLongLong();

    // Spotify often reports a stale position; keep counting ourselves and
    // take the player's value whenever it actually moves.
    qint64 reported = all.value("Position").toLongLong();
    if (trackChanged || reported != lastReportedUs_)
        positionUs_ = reported;
    else if (playing_)
        positionUs_ += 1000000;
    lastReportedUs_ = reported;
    if (lengthUs_ > 0) positionUs_ = qBound<qint64>(0, positionUs_, lengthUs_);

    if (artUrl != artUrl_) {
        artUrl_ = artUrl;
        setArt(QImage());
        QUrl url(artUrl);
        if (url.isLocalFile()) {
            setArt(QImage(url.toLocalFile()));
        } else if (url.scheme().startsWith("http")) {
            QNetworkReply *download = net_.get(QNetworkRequest(url));
            connect(download, &QNetworkReply::finished, this, [this, download, artUrl] {
                download->deleteLater();
                if (artUrl != artUrl_) return;
                setArt(QImage::fromData(download->readAll()));
                emit changed();
            });
        }
    }
    emit changed();
}

void MediaPlayers::setArt(const QImage &image) {
    art_ = image;
    if (image.isNull()) {
        backdrop_ = QImage();
        return;
    }
    // The middle strip of the art in the card's shape (wide and slim),
    // shrunk to a few pixels and back: a cheap, smooth blur.
    QImage wide = image.scaled(600, 84, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    wide = wide.copy((wide.width() - 600) / 2, (wide.height() - 84) / 2, 600, 84);
    QImage small = wide.scaled(42, 6, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    backdrop_ = small.scaled(300, 42, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void MediaPlayers::call(const QString &method) {
    if (service_.isEmpty()) return;
    QDBusInterface player(service_, kMprisPath, kPlayerInterface);
    player.asyncCall(method);
    QTimer::singleShot(300, this, &MediaPlayers::poll);
}

void MediaPlayers::setProperty(const QString &name, const QVariant &value) {
    if (service_.isEmpty()) return;
    QDBusInterface props(service_, kMprisPath, "org.freedesktop.DBus.Properties");
    props.asyncCall("Set", kPlayerInterface, name, QVariant::fromValue(QDBusVariant(value)));
    QTimer::singleShot(300, this, &MediaPlayers::poll);
}

void MediaPlayers::toggleShuffle() { setProperty("Shuffle", !shuffle_); }

void MediaPlayers::cycleLoop() {
    // Off -> whole playlist -> this song -> off, like Spotify's button.
    setProperty("LoopStatus", loop_ == Loop::None ? "Playlist" : loop_ == Loop::Playlist ? "Track" : "None");
}

void MediaPlayers::seek(double fraction) {
    if (service_.isEmpty() || lengthUs_ <= 0 || trackId_.isEmpty()) return;
    qint64 target = qint64(qBound(0.0, fraction, 1.0) * lengthUs_);
    QDBusInterface player(service_, kMprisPath, kPlayerInterface);
    player.asyncCall("SetPosition", QVariant::fromValue(QDBusObjectPath(trackId_)), target);
    positionUs_ = target;
    lastReportedUs_ = -1;
    emit changed();
}

// --- DesktopWidget ------------------------------------------------------------

DesktopWidget::DesktopWidget(QScreen *screen, const QString &name, Place place, QPoint pos, QSize size,
                             bool clickable, bool arrange)
    : arrange_(arrange), name_(name), screen_(screen) {
    setAttribute(Qt::WA_TranslucentBackground);
    if (!clickable && !arrange) setWindowFlag(Qt::WindowTransparentForInput);
    setFixedSize(size);
    setScreen(screen);
    winId();

    using LS = LayerShellQt::Window;
    layer_ = LS::get(windowHandle());
    // The same layer and kind as the wallpaper; made after it, so on top.
    layer_->setLayer(LS::LayerBottom);
    layer_->setScope("desktop");
    layer_->setKeyboardInteractivity(LS::KeyboardInteractivityNone);
    layer_->setExclusiveZone(0);
    // Always placed by its top-left corner, so it can be dragged anywhere.
    layer_->setAnchors(LS::Anchors(LS::AnchorTop | LS::AnchorLeft));

    QSize area = screen->size();
    if (pos.x() < 0 || pos.y() < 0) {
        int column = int(place) % 3, row = int(place) / 3;
        int x = column == 0 ? kEdge : column == 1 ? (area.width() - size.width()) / 2 : area.width() - size.width() - kEdge;
        int y = row == 0 ? kEdge : row == 1 ? (area.height() - size.height()) / 2
                                            : area.height() - size.height() - kBottomEdge;
        pos = QPoint(x, y);
    }
    moveTo(pos);
}

void DesktopWidget::moveTo(QPoint topLeft) {
    QSize area = screen_->size();
    topLeft.setX(qBound(0, topLeft.x(), qMax(0, area.width() - width())));
    topLeft.setY(qBound(0, topLeft.y(), qMax(0, area.height() - height())));
    topLeft_ = topLeft;
    layer_->setMargins(QMargins(topLeft.x(), topLeft.y(), 0, 0));
    update(); // the move takes effect with the next frame
}

void DesktopWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && draggableAt(event->position())) {
        dragging_ = true;
        grab_ = event->position();
        update();
    }
}

void DesktopWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!dragging_) return;
    // The point grabbed stays under the mouse.
    QPointF delta = event->position() - grab_;
    moveTo(topLeft_ + delta.toPoint());
}

void DesktopWidget::mouseReleaseEvent(QMouseEvent *) {
    if (!dragging_) return;
    dragging_ = false;
    Settings::savePosition(name_, topLeft_);
    update();
}

void DesktopWidget::enterEvent(QEnterEvent *) {
    hovered_ = true;
    update();
}

void DesktopWidget::leaveEvent(QEvent *) {
    hovered_ = false;
    update();
}

void DesktopWidget::paintArrangeFrame(QPainter &p) {
    if (!arrange_ && !hovered_ && !dragging_) return;
    p.save();
    QPen pen(QColor(137, 180, 250), 2, Qt::DashLine);
    p.setPen(pen);
    p.setBrush(QColor(137, 180, 250, 25));
    p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12, 12);
    QFont f = font();
    f.setPixelSize(13);
    p.setFont(f);
    p.setPen(QColor(205, 214, 244));
    p.drawText(QRectF(rect()).adjusted(10, 6, -10, -6), Qt::AlignTop | Qt::AlignLeft, "Drag to move");
    p.restore();
}

// --- ClockWidget ----------------------------------------------------------------

ClockWidget::ClockWidget(QScreen *screen, const Settings &settings)
    : DesktopWidget(screen, "clock", settings.clockPlace, settings.clockPos,
                    QSize(int(1000 * settings.clockSize / 100.0), int(300 * settings.clockSize / 100.0)), true,
                    settings.arrange),
      settings_(settings), scale_(settings.clockSize / 100.0) {
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
    paintArrangeFrame(p);
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
    QString dayText = english.toString(now, "dddd").toUpper();
    QString dateText = english.toString(now, "d MMMM, yyyy").toUpper();
    QString timeText = "- " + time + " -";
    p.setFont(day);
    drawText(p, QRectF(0, top, area.width(), dayHeight), Qt::AlignCenter, dayText, color);
    QRect dayBox = QFontMetrics(day).boundingRect(QRect(0, int(top), width(), int(dayHeight)), Qt::AlignCenter, dayText);
    p.setFont(small);
    drawText(p, QRectF(0, top + dayHeight, area.width(), lineHeight), Qt::AlignCenter, dateText, color);
    drawText(p, QRectF(0, top + dayHeight + lineHeight, area.width(), lineHeight), Qt::AlignCenter, timeText, color);
    QFontMetrics smallMetrics(small);
    QRect textBox = dayBox
                        .united(smallMetrics.boundingRect(QRect(0, int(top + dayHeight), width(), int(lineHeight)),
                                                          Qt::AlignCenter, dateText))
                        .united(smallMetrics.boundingRect(
                            QRect(0, int(top + dayHeight + lineHeight), width(), int(lineHeight)), Qt::AlignCenter,
                            timeText))
                        .adjusted(-12, -8, 12, 8);
    // Only the text takes the mouse (to drag the clock); clicks on the
    // empty space around it still reach the desktop.
    if (textBox != inputBox_) {
        inputBox_ = textBox;
        windowHandle()->setMask(QRegion(textBox));
    }
}

// --- MusicWidget ----------------------------------------------------------------
//
// A slim card in the style of guns.lol / haunt.gg profile players: the
// blurred album art behind, the cover on the left, an equalizer icon with
// the title, the artist, and one row of previous / play / next, elapsed
// time, a thin progress bar and the length.

MusicWidget::MusicWidget(QScreen *screen, const Settings &settings, MediaPlayers *players)
    : DesktopWidget(screen, "music", settings.musicPlace, settings.musicPos,
                    QSize(int(600 * settings.musicSize / 100.0), int(84 * settings.musicSize / 100.0)), true,
                    settings.arrange),
      settings_(settings), players_(players), scale_(settings.musicSize / 100.0) {
    auto refresh = [this] {
        // Nothing playing: nothing shown.
        setVisible(players_->active() || arrange_);
        // The equalizer icon only moves while playing.
        if (players_->playing() && !animation_.isActive())
            animation_.start(140);
        else if (!players_->playing())
            animation_.stop();
        update();
    };
    connect(players_, &MediaPlayers::changed, this, refresh);
    connect(&animation_, &QTimer::timeout, this, [this] { update(); });
    refresh();
}

QRectF MusicWidget::progressBar() const {
    double s = scale_;
    double left = 216 * s, right = width() - 52 * s;
    return QRectF(left, 62 * s, right - left, 4 * s);
}

QRectF MusicWidget::button(int index) const {
    double s = scale_;
    switch (index) {
    case Previous: return QRectF(84 * s, 57 * s, 14 * s, 14 * s);
    case PlayPause: return QRectF(110 * s, 48 * s, 32 * s, 32 * s);
    default: return QRectF(154 * s, 57 * s, 14 * s, 14 * s);
    }
}

bool MusicWidget::draggableAt(QPointF pos) const {
    for (int i = 0; i < ButtonCount; ++i)
        if (button(i).adjusted(-6, -6, 6, 6).contains(pos)) return false;
    return !progressBar().adjusted(0, -10, 0, 10).contains(pos);
}

void MusicWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    double s = scale_;
    QColor color(settings_.musicColor);
    QColor dim = color;
    dim.setAlphaF(0.7f);

    // No card: everything sits straight on the wallpaper, with a soft
    // shadow under text and icons so they stay readable.
    const QColor shadow(0, 0, 0, 120);

    // Cover.
    QRectF art(14 * s, 14 * s, 56 * s, 56 * s);
    if (!players_->art().isNull())
        p.drawImage(art, players_->art());
    else
        p.fillRect(art, QColor(255, 255, 255, 25));

    // Equalizer icon, then the title; the artist below.
    double left = 84 * s, textWidth = width() - left - 16 * s;
    p.setPen(Qt::NoPen);
    qint64 t = QDateTime::currentMSecsSinceEpoch() / 140;
    for (int pass = 0; pass < 2; ++pass) {
        p.setBrush(pass == 0 ? shadow : color);
        double dy = pass == 0 ? 1.5 * s : 0;
        for (int i = 0; i < 3; ++i) {
            // Three bars; while playing they bounce.
            static const double still[3] = {0.55, 1.0, 0.75};
            double h = players_->playing() ? 0.35 + 0.65 * ((t * (i + 2) + i * 3) % 7) / 6.0 : still[i];
            double barHeight = 13 * s * h;
            p.drawRoundedRect(QRectF(left + i * 4.5 * s, 25 * s - barHeight + dy, 2.6 * s, barHeight), 1, 1);
        }
    }
    double titleLeft = left + 20 * s;
    QFont title = font();
    title.setPixelSize(int(16 * s));
    title.setWeight(QFont::Medium);
    p.setFont(title);
    drawText(p, QRectF(titleLeft, 10 * s, width() - titleLeft - 16 * s, 20 * s), Qt::AlignLeft | Qt::AlignVCenter,
             QFontMetrics(title).elidedText(players_->active() ? players_->title() : QString("Nothing playing"),
                                            Qt::ElideRight, int(width() - titleLeft - 16 * s)),
             color);
    QFont artist = font();
    artist.setPixelSize(int(13 * s));
    p.setFont(artist);
    drawText(p, QRectF(left, 30 * s, textWidth, 18 * s), Qt::AlignLeft | Qt::AlignVCenter,
             QFontMetrics(artist).elidedText(players_->artist(), Qt::ElideRight, int(textWidth)), dim);

    // Previous, play/pause (white circle), next.
    p.setPen(Qt::NoPen);
    for (int pass = 0; pass < 2; ++pass) {
        double dy = pass == 0 ? 1.5 * s : 0;
        p.setBrush(pass == 0 ? shadow : color);
        for (int i : {int(Previous), int(Next)}) {
            QRectF b = button(i);
            QPointF c = b.center() + QPointF(0, dy);
            double r = b.width() / 2, dir = i == Previous ? -1 : 1;
            p.drawPolygon(QPolygonF{{c.x() - dir * r * 0.5, c.y() - r * 0.75},
                                    {c.x() + dir * r * 0.5, c.y()},
                                    {c.x() - dir * r * 0.5, c.y() + r * 0.75}});
            p.drawRect(QRectF(c.x() + dir * r * 0.55 - r * 0.12, c.y() - r * 0.75, r * 0.24, r * 1.5));
        }
    }
    QRectF play = button(PlayPause);
    QPointF c = play.center();
    double r = play.width() / 2;
    p.setBrush(shadow);
    p.drawEllipse(c + QPointF(0, 1.5 * s), r, r);
    p.setBrush(QColor(255, 255, 255));
    p.drawEllipse(c, r, r);
    p.setBrush(QColor(10, 10, 14));
    if (players_->playing()) {
        p.drawRoundedRect(QRectF(c.x() - r * 0.32, c.y() - r * 0.38, r * 0.22, r * 0.76), 1.5, 1.5);
        p.drawRoundedRect(QRectF(c.x() + r * 0.1, c.y() - r * 0.38, r * 0.22, r * 0.76), 1.5, 1.5);
    } else {
        p.drawPolygon(QPolygonF{{c.x() - r * 0.22, c.y() - r * 0.42}, {c.x() + r * 0.44, c.y()},
                                {c.x() - r * 0.22, c.y() + r * 0.42}});
    }

    // Elapsed, the bar with its knob, length.
    QRectF bar = progressBar();
    QFont times = font();
    times.setPixelSize(int(12 * s));
    p.setFont(times);
    drawText(p, QRectF(176 * s, bar.center().y() - 9 * s, 36 * s, 18 * s), Qt::AlignLeft | Qt::AlignVCenter,
             clockTime(players_->positionUs()), dim);
    drawText(p, QRectF(width() - 48 * s, bar.center().y() - 9 * s, 36 * s, 18 * s), Qt::AlignRight | Qt::AlignVCenter,
             clockTime(players_->lengthUs()), dim);
    p.setPen(Qt::NoPen);
    p.setBrush(shadow);
    p.drawRoundedRect(bar.translated(0, 1.5 * s), bar.height() / 2, bar.height() / 2);
    p.setBrush(QColor(255, 255, 255, 70));
    p.drawRoundedRect(bar, bar.height() / 2, bar.height() / 2);
    double done = players_->lengthUs() > 0 ? double(players_->positionUs()) / double(players_->lengthUs()) : 0;
    done = qBound(0.0, done, 1.0);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(bar.left(), bar.top(), qMax(bar.height(), bar.width() * done), bar.height()),
                      bar.height() / 2, bar.height() / 2);
    p.drawEllipse(QPointF(bar.left() + bar.width() * done, bar.center().y()), 5.5 * s, 5.5 * s);

    paintArrangeFrame(p);
}

void MusicWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPointF pos = event->position();
        if (button(Previous).adjusted(-6, -6, 6, 6).contains(pos)) return players_->previous();
        if (button(PlayPause).adjusted(-4, -4, 4, 4).contains(pos)) return players_->playPause();
        if (button(Next).adjusted(-6, -6, 6, 6).contains(pos)) return players_->next();
        QRectF bar = progressBar();
        if (bar.adjusted(0, -10, 0, 10).contains(pos))
            return players_->seek((pos.x() - bar.left()) / bar.width());
    }
    DesktopWidget::mousePressEvent(event);
}

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
    // Shrinking to a few pixels and back is a cheap, smooth blur.
    QImage small = image.scaled(18, 18, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    backdrop_ = small.scaled(220, 84, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
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
}

void DesktopWidget::paintArrangeFrame(QPainter &p) {
    if (!arrange_) return;
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
// In the style of the profile players on guns.lol / haunt.gg and Rainmeter
// Spotify skins: a glass card over the blurred album art, the cover, title
// and artist, a thin progress bar with times, and a row of white controls
// around a round play button.

MusicWidget::MusicWidget(QScreen *screen, const Settings &settings, MediaPlayers *players)
    : DesktopWidget(screen, "music", settings.musicPlace, settings.musicPos,
                    QSize(int(440 * settings.musicSize / 100.0), int(172 * settings.musicSize / 100.0)), true,
                    settings.arrange),
      settings_(settings), players_(players), scale_(settings.musicSize / 100.0) {
    auto refresh = [this] {
        // Nothing playing: nothing shown (unless it is being arranged).
        setVisible(players_->active() || arrange_);
        update();
    };
    connect(players_, &MediaPlayers::changed, this, refresh);
    refresh();
}

QRectF MusicWidget::progressBar() const {
    double s = scale_;
    return QRectF(58 * s, 112 * s, width() - 116 * s, 4 * s);
}

QRectF MusicWidget::button(int index) const {
    double s = scale_;
    // Shuffle, previous, play, next, repeat: centred under the card.
    static const double sizes[ButtonCount] = {20, 24, 42, 24, 20};
    double gap = 26 * s, total = 0;
    for (double size : sizes) total += size * s;
    total += gap * (ButtonCount - 1);
    double x = (width() - total) / 2, centre = 146 * s;
    for (int i = 0; i < index; ++i) x += sizes[i] * s + gap;
    double size = sizes[index] * s;
    return QRectF(x, centre - size / 2, size, size);
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
    dim.setAlphaF(0.65f);

    // Glass card: blurred album art, darkened, with a faint edge.
    QPainterPath card;
    card.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 20 * s, 20 * s);
    p.save();
    p.setClipPath(card);
    if (!players_->backdrop().isNull())
        p.drawImage(rect(), players_->backdrop());
    p.fillRect(rect(), QColor(10, 10, 16, players_->backdrop().isNull() ? 190 : 150));
    p.restore();
    p.setPen(QPen(QColor(255, 255, 255, 34), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(card);

    // Cover.
    QRectF art(18 * s, 18 * s, 76 * s, 76 * s);
    QPainterPath artClip;
    artClip.addRoundedRect(art, 12 * s, 12 * s);
    if (!players_->art().isNull()) {
        p.save();
        p.setClipPath(artClip);
        p.drawImage(art, players_->art());
        p.restore();
    } else {
        p.fillPath(artClip, QColor(255, 255, 255, 28));
    }

    // Title and artist.
    double left = 110 * s, textWidth = width() - left - 18 * s;
    QFont title = font();
    title.setPixelSize(int(18 * s));
    title.setWeight(QFont::DemiBold);
    p.setFont(title);
    p.setPen(color);
    QString titleText = players_->active() ? players_->title() : QString("Nothing playing");
    p.drawText(QRectF(left, 30 * s, textWidth, 26 * s), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(title).elidedText(titleText, Qt::ElideRight, int(textWidth)));
    QFont artist = font();
    artist.setPixelSize(int(14 * s));
    p.setFont(artist);
    p.setPen(dim);
    p.drawText(QRectF(left, 58 * s, textWidth, 20 * s), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(artist).elidedText(players_->artist(), Qt::ElideRight, int(textWidth)));

    // Progress: elapsed, a thin rounded bar with a knob, length.
    QFont times = font();
    times.setPixelSize(int(11 * s));
    p.setFont(times);
    QRectF bar = progressBar();
    p.drawText(QRectF(14 * s, bar.center().y() - 8 * s, 40 * s, 16 * s), Qt::AlignCenter,
               clockTime(players_->positionUs()));
    p.drawText(QRectF(width() - 54 * s, bar.center().y() - 8 * s, 40 * s, 16 * s), Qt::AlignCenter,
               clockTime(players_->lengthUs()));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 55));
    p.drawRoundedRect(bar, bar.height() / 2, bar.height() / 2);
    double done = players_->lengthUs() > 0 ? double(players_->positionUs()) / double(players_->lengthUs()) : 0;
    done = qBound(0.0, done, 1.0);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * done, bar.height()), bar.height() / 2,
                      bar.height() / 2);
    p.drawEllipse(QPointF(bar.left() + bar.width() * done, bar.center().y()), 5 * s, 5 * s);

    // Controls.
    auto pen = [&](const QColor &c) {
        QPen line(c, 2 * s);
        line.setCapStyle(Qt::RoundCap);
        line.setJoinStyle(Qt::RoundJoin);
        return line;
    };
    for (int i = 0; i < ButtonCount; ++i) {
        QRectF b = button(i);
        QPointF c = b.center();
        double r = b.width() / 2;
        p.setBrush(Qt::NoBrush);
        switch (i) {
        case Shuffle: {
            QColor on = players_->shuffle() ? kAccent : dim;
            p.setPen(pen(on));
            p.drawLine(QPointF(c.x() - r, c.y() - r * 0.55), QPointF(c.x() + r * 0.8, c.y() + r * 0.55));
            p.drawLine(QPointF(c.x() - r, c.y() + r * 0.55), QPointF(c.x() + r * 0.8, c.y() - r * 0.55));
            p.drawPolyline(QPolygonF{{c.x() + r * 0.35, c.y() - r * 0.75}, {c.x() + r * 0.85, c.y() - r * 0.55},
                                     {c.x() + r * 0.6, c.y() - r * 0.1}});
            p.drawPolyline(QPolygonF{{c.x() + r * 0.35, c.y() + r * 0.75}, {c.x() + r * 0.85, c.y() + r * 0.55},
                                     {c.x() + r * 0.6, c.y() + r * 0.1}});
            if (players_->shuffle()) {
                p.setPen(Qt::NoPen);
                p.setBrush(kAccent);
                p.drawEllipse(QPointF(c.x(), b.bottom() + 5 * s), 1.6 * s, 1.6 * s);
            }
            break;
        }
        case Previous:
        case Next: {
            double dir = i == Previous ? -1 : 1;
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawPolygon(QPolygonF{{c.x() - dir * r * 0.55, c.y() - r * 0.6},
                                    {c.x() + dir * r * 0.45, c.y()},
                                    {c.x() - dir * r * 0.55, c.y() + r * 0.6}});
            p.drawRoundedRect(QRectF(c.x() + dir * r * 0.5 - r * 0.1, c.y() - r * 0.6, r * 0.2, r * 1.2), 1, 1);
            break;
        }
        case PlayPause: {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255));
            p.drawEllipse(c, r, r);
            p.setBrush(QColor(12, 12, 18));
            if (players_->playing()) {
                p.drawRoundedRect(QRectF(c.x() - r * 0.32, c.y() - r * 0.36, r * 0.22, r * 0.72), 1.5, 1.5);
                p.drawRoundedRect(QRectF(c.x() + r * 0.1, c.y() - r * 0.36, r * 0.22, r * 0.72), 1.5, 1.5);
            } else {
                p.drawPolygon(QPolygonF{{c.x() - r * 0.22, c.y() - r * 0.4}, {c.x() + r * 0.42, c.y()},
                                        {c.x() - r * 0.22, c.y() + r * 0.4}});
            }
            break;
        }
        case Repeat: {
            bool on = players_->loop() != MediaPlayers::Loop::None;
            p.setPen(pen(on ? kAccent : dim));
            QRectF loop(c.x() - r * 0.9, c.y() - r * 0.5, r * 1.8, r * 1.0);
            p.drawRoundedRect(loop, r * 0.35, r * 0.35);
            p.setPen(Qt::NoPen);
            p.setBrush(on ? kAccent : dim);
            p.drawPolygon(QPolygonF{{loop.center().x() + r * 0.1, loop.top() - r * 0.3},
                                    {loop.center().x() + r * 0.45, loop.top()},
                                    {loop.center().x() + r * 0.1, loop.top() + r * 0.3}});
            if (players_->loop() == MediaPlayers::Loop::Track) {
                QFont one = font();
                one.setPixelSize(int(9 * s));
                one.setBold(true);
                p.setFont(one);
                p.setPen(kAccent);
                p.drawText(loop, Qt::AlignCenter, "1");
            }
            if (on) p.drawEllipse(QPointF(c.x(), b.bottom() + 5 * s), 1.6 * s, 1.6 * s);
            break;
        }
        }
    }
    paintArrangeFrame(p);
}

void MusicWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPointF pos = event->position();
        if (button(Shuffle).adjusted(-6, -6, 6, 6).contains(pos)) return players_->toggleShuffle();
        if (button(Previous).adjusted(-6, -6, 6, 6).contains(pos)) return players_->previous();
        if (button(PlayPause).adjusted(-4, -4, 4, 4).contains(pos)) return players_->playPause();
        if (button(Next).adjusted(-6, -6, 6, 6).contains(pos)) return players_->next();
        if (button(Repeat).adjusted(-6, -6, 6, 6).contains(pos)) return players_->cycleLoop();
        QRectF bar = progressBar();
        if (bar.adjusted(0, -10, 0, 10).contains(pos))
            return players_->seek((pos.x() - bar.left()) / bar.width());
    }
    DesktopWidget::mousePressEvent(event);
}

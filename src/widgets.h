// Desktop widgets drawn over the wallpaper: a Mond-style clock and a music
// player. Each is its own small, transparent layer-shell surface just
// above the wallpaper, so it also shows over video wallpapers. Both can be
// dragged around: the player by its card, the clock by its text.
#pragma once

#include "settings.h"

#include <QImage>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QWidget>

class QScreen;
namespace LayerShellQt {
class Window;
}

// What a media player is playing, read over MPRIS (the desktop's shared
// media-player interface): Spotify when it runs, otherwise any player.
class MediaPlayers : public QObject {
    Q_OBJECT
public:
    explicit MediaPlayers(QObject *parent = nullptr);

    enum class Loop { None, Track, Playlist };

    bool active() const { return !service_.isEmpty() && !title_.isEmpty(); }
    QString title() const { return title_; }
    QString artist() const { return artist_; }
    bool playing() const { return playing_; }
    bool shuffle() const { return shuffle_; }
    Loop loop() const { return loop_; }
    qint64 positionUs() const { return positionUs_; }
    qint64 lengthUs() const { return lengthUs_; }
    QImage art() const { return art_; }
    // The album art, blurred and darkened, for the card's background.
    QImage backdrop() const { return backdrop_; }

    void playPause() { call("PlayPause"); }
    void next() { call("Next"); }
    void previous() { call("Previous"); }
    void toggleShuffle();
    void cycleLoop();
    void seek(double fraction);

signals:
    void changed();

private:
    void poll();
    void call(const QString &method);
    void setProperty(const QString &name, const QVariant &value);
    void setArt(const QImage &image);
    QString pickPlayer() const;

    QTimer timer_;
    QNetworkAccessManager net_;
    QString service_, title_, artist_, artUrl_, trackId_;
    bool playing_ = false, shuffle_ = false;
    Loop loop_ = Loop::None;
    qint64 positionUs_ = 0, lengthUs_ = 0, lastReportedUs_ = -1;
    QImage art_, backdrop_;
};

// A transparent surface on the wallpaper. It sits at its Position setting,
// or wherever it was last dragged to.
class DesktopWidget : public QWidget {
public:
    DesktopWidget(QScreen *screen, const QString &name, Place place, QPoint pos, QSize size, bool clickable,
                  bool arrange);

protected:
    // True when a press at `pos` starts a drag (not on a button).
    virtual bool draggableAt(QPointF) const { return true; }
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintArrangeFrame(QPainter &p);

    bool arrange_;

private:
    void moveTo(QPoint topLeft);

    QString name_;
    QScreen *screen_;
    LayerShellQt::Window *layer_;
    QPoint topLeft_;
    bool dragging_ = false;
    QPointF grab_;
};

class ClockWidget : public DesktopWidget {
public:
    ClockWidget(QScreen *screen, const Settings &settings);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QFont clockFont(double pixelSize, int weight) const;

    Settings settings_;
    QTimer tick_;
    double scale_;
    QRect inputBox_;
};

class MusicWidget : public DesktopWidget {
public:
    MusicWidget(QScreen *screen, const Settings &settings, MediaPlayers *players);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool draggableAt(QPointF pos) const override;

private:
    enum Button { Shuffle, Previous, PlayPause, Next, Repeat, ButtonCount };
    QRectF button(int index) const;
    QRectF progressBar() const;

    Settings settings_;
    MediaPlayers *players_;
    double scale_;
};

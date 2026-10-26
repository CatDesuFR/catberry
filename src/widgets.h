// Desktop widgets drawn over the wallpaper: a Mond-style clock and a music
// player. Each is its own small, transparent layer-shell surface just
// above the wallpaper, so it also shows over video wallpapers.
#pragma once

#include "settings.h"

#include <QImage>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QWidget>

class QScreen;

// What a media player is playing, read over MPRIS (the desktop's shared
// media-player interface): Spotify when it runs, otherwise any player.
class MediaPlayers : public QObject {
    Q_OBJECT
public:
    explicit MediaPlayers(QObject *parent = nullptr);

    bool active() const { return !service_.isEmpty(); }
    QString title() const { return title_; }
    QString artist() const { return artist_; }
    bool playing() const { return playing_; }
    qint64 positionUs() const { return positionUs_; }
    qint64 lengthUs() const { return lengthUs_; }
    QImage art() const { return art_; }

    void playPause() { call("PlayPause"); }
    void next() { call("Next"); }
    void previous() { call("Previous"); }

signals:
    void changed();

private:
    void poll();
    void call(const QString &method);
    QString pickPlayer() const;

    QTimer timer_;
    QNetworkAccessManager net_;
    QString service_, title_, artist_, artUrl_;
    bool playing_ = false;
    qint64 positionUs_ = 0, lengthUs_ = 0, lastReportedUs_ = -1;
    QImage art_;
};

// A transparent surface on the wallpaper, placed by a Place setting.
class DesktopWidget : public QWidget {
public:
    DesktopWidget(QScreen *screen, Place place, bool clickable);
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
};

class MusicWidget : public DesktopWidget {
public:
    MusicWidget(QScreen *screen, const Settings &settings, MediaPlayers *players);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRectF button(int index) const;

    Settings settings_;
    MediaPlayers *players_;
    double scale_;
};

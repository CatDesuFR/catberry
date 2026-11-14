#include "wallpaper.h"

#include "fonts.h"
#include "library.h"
#include "settings.h"
#include "track.h"
#include "widgets.h"

#include <LayerShellQt/window.h>

#include <QApplication>
#include <QAudioOutput>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMediaPlayer>
#include <QMovie>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QThread>
#include <QVideoWidget>
#include <QWidget>
#include <QWindow>

#include <malloc.h>
#include <unistd.h>

#include <cstdio>

#include <memory>

namespace {

QString serverName() { return "catberry-roofce-" + QString::number(getuid()); }

// One screen's wallpaper.
class WallpaperView : public QWidget {
public:
    WallpaperView(QScreen *screen, bool withSound) : withSound_(withSound) {
        setAttribute(Qt::WA_OpaquePaintEvent);
        // Clicks go through to the desktop underneath (its right-click menu).
        setWindowFlag(Qt::WindowTransparentForInput);
        setScreen(screen);
        setGeometry(screen->geometry());
        winId(); // the native window must exist before it gets its role

        auto *layer = LayerShellQt::Window::get(windowHandle());
        // KDE's own desktop (Plasma's wallpaper) sits in the background
        // layer and can be raised over anything else there, so the
        // wallpaper goes one layer up: above it, below every window.
        layer->setLayer(LayerShellQt::Window::LayerBottom);
        layer->setAnchors(LayerShellQt::Window::Anchors(
            LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
            LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        // -1: cover the whole screen, ignoring panels' reserved space.
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        // KWin treats a surface named "desktop" as part of the desktop: Show
        // Desktop (Meta+D) keeps it instead of hiding it with the windows.
        layer->setScope("desktop");
    }

    // `play` is the file to show: the setting's file, or a copy of a video
    // scaled down to the screen.
    void apply(const Settings &s, const QString &play) {
        settings_ = s;
        movie_.reset();
        if (player_) player_->stop();
        image_ = QImage();
        Kind kind = kindOfFile(s.file);
        if (kind == Kind::Video) {
            if (!player_) {
                player_ = new QMediaPlayer(this);
                video_ = new QVideoWidget(this);
                player_->setVideoOutput(video_);
                player_->setLoops(QMediaPlayer::Infinite);
            }
            // Sound only when it is on (and on one screen): without an audio
            // output the sound is not even decoded.
            bool sound = s.sound && withSound_;
            if (sound && !audio_) {
                audio_ = new QAudioOutput(this);
                player_->setAudioOutput(audio_);
            } else if (!sound && audio_) {
                player_->setAudioOutput(nullptr);
                delete audio_;
                audio_ = nullptr;
            }
            video_->setAspectRatioMode(s.fit == "fit" || s.fit == "center" ? Qt::KeepAspectRatio
                                       : s.fit == "stretch"                 ? Qt::IgnoreAspectRatio
                                                                            : Qt::KeepAspectRatioByExpanding);
            video_->setGeometry(rect());
            video_->show();
            if (audio_) audio_->setVolume(s.volume / 100.0f);
            player_->setSource(QUrl::fromLocalFile(play));
            player_->play();
        } else {
            if (video_) video_->hide();
            if (kind == Kind::Gif) {
                movie_ = std::make_unique<QMovie>(s.file);
                movie_->setCacheMode(QMovie::CacheNone);
                QObject::connect(movie_.get(), &QMovie::frameChanged, this, [this] { update(); });
                movie_->start();
            } else if (kind == Kind::Image) {
                image_ = loadForScreen(s);
            }
        }
        update();
    }

protected:
    void resizeEvent(QResizeEvent *) override {
        if (video_) video_->setGeometry(rect());
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        QImage frame = movie_ ? movie_->currentImage() : image_;
        if (frame.isNull()) return;
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QSize size = frame.size();
        if (settings_.fit == "stretch")
            size = this->size();
        else if (settings_.fit == "fit")
            size.scale(this->size(), Qt::KeepAspectRatio);
        else if (settings_.fit == "fill")
            size.scale(this->size(), Qt::KeepAspectRatioByExpanding);
        // "center" keeps the original size.
        QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        p.drawImage(target, frame);
    }

private:
    // Big pictures are shrunk to the screen as they load: an 8K picture
    // would otherwise hold about 130 MB of memory, at screen size about 8.
    QImage loadForScreen(const Settings &s) const {
        QImageReader reader(s.file);
        reader.setAutoTransform(true);
        QSize original = reader.size(), screen = size();
        if (original.isValid() && (original.width() > screen.width() || original.height() > screen.height())) {
            if (s.fit == "center") {
                // Only the middle part is ever visible.
                reader.setClipRect(QRect((original.width() - screen.width()) / 2,
                                         (original.height() - screen.height()) / 2, screen.width(), screen.height())
                                       .intersected(QRect(QPoint(), original)));
            } else {
                QSize scaled = s.fit == "stretch" ? screen
                               : s.fit == "fit"   ? original.scaled(screen, Qt::KeepAspectRatio)
                                                  : original.scaled(screen, Qt::KeepAspectRatioByExpanding);
                reader.setScaledSize(scaled);
            }
        }
        return reader.read();
    }

    bool withSound_;
    Settings settings_;
    QImage image_;
    std::unique_ptr<QMovie> movie_;
    QMediaPlayer *player_ = nullptr;
    QVideoWidget *video_ = nullptr;
    QAudioOutput *audio_ = nullptr;
};

class WallpaperScreens : public QObject {
public:
    WallpaperScreens() {
        connect(qApp, &QGuiApplication::screenAdded, this, [this] { rebuild(); });
        connect(qApp, &QGuiApplication::screenRemoved, this, [this] { rebuild(); });
        rebuild();
    }

    void reload() {
        Settings s = Settings::load();
        QString play = kindOfFile(s.file) == Kind::Video ? screenCopy(s.file) : s.file;
        for (auto &view : views_) view->apply(s, play);
        // The song that plays with the wallpaper. It keeps playing through
        // reloads; only a different file starts it again.
        if (s.track && !s.trackFile.isEmpty() && QFileInfo::exists(s.trackFile)) {
            if (!track_) track_ = std::make_unique<TrackPlayer>();
            track_->setVolume(s.trackVolume);
            track_->setFile(s.trackFile);
        } else {
            if (players_) players_->setLocal(nullptr);
            track_.reset();
        }
        // The widgets go on the main screen, made after the wallpaper so
        // they sit on top of it.
        clock_.reset();
        music_.reset();
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen) return;
        FontLibrary::registerAll(); // downloaded and imported fonts
        if (s.clock) {
            clock_ = std::make_unique<ClockWidget>(screen, s);
            clock_->show();
        }
        if (s.music) {
            if (!players_) players_ = std::make_unique<MediaPlayers>();
            players_->setLocal(track_.get());
            music_ = std::make_unique<MusicWidget>(screen, s, players_.get());
        } else {
            players_.reset();
        }
    }

private:
    // The biggest screen; videos are scaled to cover it.
    QSize screenSize() const {
        QSize biggest;
        for (QScreen *screen : QGuiApplication::screens()) {
            QSize size = screen->size() * screen->devicePixelRatio();
            if (size.width() * size.height() > biggest.width() * biggest.height()) biggest = size;
        }
        return biggest;
    }

    static QSize videoSize(const QString &file) {
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
                                "-of", "csv=s=x:p=0", file});
        if (!probe.waitForFinished(5000)) return QSize();
        QStringList parts = QString::fromUtf8(probe.readAllStandardOutput()).trimmed().split('x');
        return parts.size() == 2 ? QSize(parts[0].toInt(), parts[1].toInt()) : QSize();
    }

    // Decoding a video bigger than the screen costs memory for pixels that
    // are never seen (a 4K video on a 1080p screen: about twice the memory
    // of a 1080p one). So a copy scaled to the screen is made once, in the
    // background, and used from then on. Until it is ready, the original
    // plays.
    QString screenCopy(const QString &file) {
        QSize screen = screenSize(), video = videoSize(file);
        if (!screen.isValid() || !video.isValid() || QStandardPaths::findExecutable("ffmpeg").isEmpty()) return file;
        // Already about the screen's size or smaller.
        if (video.width() <= screen.width() * 11 / 10 && video.height() <= screen.height() * 11 / 10) return file;

        QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/catberry/roofce/scaled";
        QDir().mkpath(dir);
        QFileInfo info(file);
        QByteArray key = (info.absoluteFilePath() + QString::number(info.lastModified().toSecsSinceEpoch())).toUtf8();
        QString copy = dir + "/" + QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(16) + "-" +
                       QString("%1x%2").arg(screen.width()).arg(screen.height()) + ".mp4";
        if (QFile::exists(copy)) return copy;
        if (converting_ == copy) return file;

        // Only the current wallpaper's copy is kept.
        for (const QFileInfo &old : QDir(dir).entryInfoList(QDir::Files)) QFile::remove(old.absoluteFilePath());
        delete converter_;
        converter_ = new QProcess(this);
        converting_ = copy;
        QString scale = QString("scale=%1:%2:force_original_aspect_ratio=increase,scale=trunc(iw/2)*2:trunc(ih/2)*2")
                            .arg(screen.width())
                            .arg(screen.height());
        // Low priority and few threads, so it does not slow anything down.
        QStringList args = {"-n", "19", "ffmpeg", "-v", "error", "-y", "-i", file, "-vf", scale, "-c:v", "libx264",
                            "-preset", "veryfast", "-crf", "21", "-pix_fmt", "yuv420p", "-c:a", "copy",
                            "-movflags", "+faststart", "-threads", "4", copy + ".part.mp4"};
        connect(converter_, &QProcess::finished, this, [this, file, copy](int code, QProcess::ExitStatus status) {
            converting_.clear();
            if (status != QProcess::NormalExit || code != 0) {
                QFile::remove(copy + ".part.mp4");
                return;
            }
            QFile::rename(copy + ".part.mp4", copy);
            // Switch over if it is still the wallpaper.
            if (Settings::load().file == file) reload();
        });
        converter_->start("nice", args);
        return file;
    }

    QProcess *converter_ = nullptr;
    QString converting_;

    void rebuild() {
        views_.clear();
        bool first = true;
        for (QScreen *screen : QGuiApplication::screens()) {
            auto view = std::make_unique<WallpaperView>(screen, first);
            first = false;
            view->show();
            views_.push_back(std::move(view));
        }
        reload();
    }

    std::vector<std::unique_ptr<WallpaperView>> views_;
    std::unique_ptr<TrackPlayer> track_;
    std::unique_ptr<MediaPlayers> players_;
    std::unique_ptr<ClockWidget> clock_;
    std::unique_ptr<MusicWidget> music_;
};

bool send(const QByteArray &command) {
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(500)) return false;
    socket.write(command + "\n");
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    return true;
}

} // namespace

bool wallpaperRunning() {
    QLocalSocket socket;
    socket.connectToServer(serverName());
    return socket.waitForConnected(300);
}

// The running wallpaper process of this user, or 0.
static pid_t wallpaperPid() {
    for (const QFileInfo &entry : QDir("/proc").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool number = false;
        pid_t pid = entry.fileName().toInt(&number);
        if (!number || pid == getpid() || entry.ownerId() != getuid()) continue;
        QFile cmdline(entry.absoluteFilePath() + "/cmdline");
        if (!cmdline.open(QIODevice::ReadOnly)) continue;
        QList<QByteArray> args = cmdline.readAll().split('\0');
        if (args.size() >= 2 && args[0].endsWith("catberry-roofce") && args[1] == "--wallpaper") return pid;
    }
    return 0;
}

bool restartOutdatedWallpaper() {
    pid_t pid = wallpaperPid();
    if (!pid) return false;
    // A replaced program shows as "<path> (deleted)".
    char target[4096];
    ssize_t n = readlink(QString("/proc/%1/exe").arg(pid).toLocal8Bit().constData(), target, sizeof target - 1);
    if (n <= 0) return false;
    if (!QByteArray(target, int(n)).endsWith(" (deleted)")) return false;
    send("stop");
    for (int i = 0; i < 30 && wallpaperRunning(); i++) QThread::msleep(100);
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--wallpaper"});
}

bool showWallpaper(QString &error) {
    if (restartOutdatedWallpaper()) return true;  // the new one loads the settings itself
    if (send("reload")) return true;
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--wallpaper"})) {
        error = "Could not start the wallpaper process";
        return false;
    }
    return true;
}

void stopWallpaper() { send("stop"); }

int runWallpaper(int argc, char **argv) {
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        std::fprintf(stderr, "catberry roofce: the wallpaper needs a Wayland session (KDE Plasma, Hyprland, Sway...)\n");
        return 1;
    }
    // Layer-shell surfaces exist only on Wayland, never through XWayland.
    qputenv("QT_QPA_PLATFORM", "wayland");
    // Video decoding runs many threads; by default each gets its own memory
    // pool, which mostly sits unused. Two pools are plenty.
    mallopt(M_ARENA_MAX, 2);
    mallopt(M_TRIM_THRESHOLD, 128 * 1024);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("catberry-roofce");

    QLocalServer server;
    if (!server.listen(serverName())) {
        // Either a wallpaper is already running, or a crashed one left its
        // socket behind.
        if (wallpaperRunning()) return 0;
        QLocalServer::removeServer(serverName());
        if (!server.listen(serverName())) {
            std::fprintf(stderr, "catberry roofce: %s\n", qPrintable(server.errorString()));
            return 1;
        }
    }

    WallpaperScreens wallpaper;
    QObject::connect(&server, &QLocalServer::newConnection, [&] {
        while (QLocalSocket *client = server.nextPendingConnection()) {
            QPointer<QLocalSocket> guard(client);
            auto handle = [&wallpaper, guard] {
                while (guard && guard->canReadLine()) {
                    QByteArray command = guard->readLine().trimmed();
                    if (command == "reload") wallpaper.reload();
                    if (command == "stop") qApp->quit();
                }
            };
            QObject::connect(client, &QLocalSocket::readyRead, handle);
            handle(); // the command may already be waiting
            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        }
    });
    return app.exec();
}

#include "wallpaper.h"

#include "library.h"
#include "settings.h"

#include <LayerShellQt/window.h>

#include <QApplication>
#include <QAudioOutput>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMediaPlayer>
#include <QMovie>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QScreen>
#include <QVideoWidget>
#include <QWidget>
#include <QWindow>

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
        setScreen(screen);
        setGeometry(screen->geometry());
        winId(); // the native window must exist before it gets its role

        auto *layer = LayerShellQt::Window::get(windowHandle());
        layer->setLayer(LayerShellQt::Window::LayerBackground);
        layer->setAnchors(LayerShellQt::Window::Anchors(
            LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
            LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        // -1: cover the whole screen, ignoring panels' reserved space.
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setScope("catberry-roofce");
    }

    void apply(const Settings &s) {
        settings_ = s;
        movie_.reset();
        if (player_) player_->stop();
        image_ = QImage();
        Kind kind = kindOfFile(s.file);
        if (kind == Kind::Video) {
            if (!player_) {
                player_ = new QMediaPlayer(this);
                video_ = new QVideoWidget(this);
                audio_ = new QAudioOutput(this);
                player_->setVideoOutput(video_);
                player_->setAudioOutput(audio_);
                player_->setLoops(QMediaPlayer::Infinite);
            }
            video_->setAspectRatioMode(s.fit == "fit" || s.fit == "center" ? Qt::KeepAspectRatio
                                       : s.fit == "stretch"                 ? Qt::IgnoreAspectRatio
                                                                            : Qt::KeepAspectRatioByExpanding);
            video_->setGeometry(rect());
            video_->show();
            // Only one screen plays the sound.
            audio_->setMuted(!(s.sound && withSound_));
            audio_->setVolume(s.volume / 100.0f);
            player_->setSource(QUrl::fromLocalFile(s.file));
            player_->play();
        } else {
            if (video_) video_->hide();
            if (kind == Kind::Gif) {
                movie_ = std::make_unique<QMovie>(s.file);
                movie_->setCacheMode(QMovie::CacheNone);
                QObject::connect(movie_.get(), &QMovie::frameChanged, this, [this] { update(); });
                movie_->start();
            } else if (kind == Kind::Image) {
                image_.load(s.file);
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
        for (auto &view : views_) view->apply(s);
    }

private:
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

bool showWallpaper(QString &error) {
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

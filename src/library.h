// Wallpapers on this computer: the user's uploads and, when Wallpaper
// Engine is owned on Steam, its Workshop wallpapers (read where Steam keeps
// them, never copied).
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

enum class Kind { Image, Gif, Video, Unsupported };

struct Wallpaper {
    QString id;
    QString title;
    QString file;    // the file to show (empty when unsupported)
    QString preview; // an image to show in the library, may be empty
    Kind kind = Kind::Unsupported;
    QString note;    // why it cannot be used, when unsupported
    QStringList tags; // words describing it (Wallpaper Engine tags and description)
};

Kind kindOfFile(const QString &path);
QString kindName(Kind kind);

// Files from the Upload page and downloaded online wallpapers.
QList<Wallpaper> scanUploads();
// Copies `source` into the uploads folder; returns the new path, or empty
// and sets `error`.
QString importFile(const QString &source, QString &error);

// Whether Wallpaper Engine is installed in any Steam library.
bool wallpaperEngineInstalled();
// Wallpaper Engine Workshop wallpapers the user is subscribed to.
QList<Wallpaper> scanWallpaperEngine();

// A still frame of a video for the library, made with ffmpeg when it is
// installed; cached. Empty when it cannot be made, or (with create false)
// when it has not been made yet.
QString videoThumbnail(const QString &video, bool create = true);

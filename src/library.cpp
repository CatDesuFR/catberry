#include "library.h"

#include "paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {

const char *const kWallpaperEngineApp = "431960";

const QStringList kImageTypes = {"png", "jpg", "jpeg", "webp", "bmp"};
const QStringList kVideoTypes = {"mp4", "webm", "mkv", "mov", "avi", "m4v"};

// Every Steam library folder (Steam can keep games on several drives).
QStringList steamLibraries() {
    QString home = QDir::homePath();
    QStringList roots = {home + "/.local/share/Steam", home + "/.steam/steam",
                         home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"};
    QSet<QString> seen;
    QStringList libraries;
    auto add = [&](const QString &path) {
        QString real = QFileInfo(path).canonicalFilePath();
        if (!real.isEmpty() && QDir(real + "/steamapps").exists() && !seen.contains(real)) {
            seen.insert(real);
            libraries << real;
        }
    };
    for (const QString &root : roots) {
        add(root);
        // Extra libraries are listed as "path" "/some/dir" entries.
        QFile vdf(root + "/steamapps/libraryfolders.vdf");
        if (!vdf.open(QIODevice::ReadOnly)) continue;
        static const QRegularExpression pathLine("\"path\"\\s+\"([^\"]+)\"");
        auto it = pathLine.globalMatch(QString::fromUtf8(vdf.readAll()));
        while (it.hasNext()) add(it.next().captured(1).replace("\\\\", "\\"));
    }
    return libraries;
}

} // namespace

Kind kindOfFile(const QString &path) {
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "gif") return Kind::Gif;
    if (kImageTypes.contains(ext)) return Kind::Image;
    if (kVideoTypes.contains(ext)) return Kind::Video;
    return Kind::Unsupported;
}

QString kindName(Kind kind) {
    switch (kind) {
    case Kind::Image: return "Image";
    case Kind::Gif: return "GIF";
    case Kind::Video: return "Video";
    case Kind::Unsupported: break;
    }
    return "Not supported";
}

QList<Wallpaper> scanUploads() {
    QList<Wallpaper> out;
    // Uploaded files and online wallpapers you have downloaded, newest first.
    QFileInfoList files = QDir(paths::uploads()).entryInfoList(QDir::Files) +
                          QDir(paths::downloads()).entryInfoList(QDir::Files);
    std::sort(files.begin(), files.end(),
              [](const QFileInfo &a, const QFileInfo &b) { return a.lastModified() > b.lastModified(); });
    for (const QFileInfo &info : files) {
        if (info.suffix() == "part") continue; // a download still going
        Wallpaper w;
        w.id = info.fileName();
        w.title = info.completeBaseName();
        w.file = info.absoluteFilePath();
        w.kind = kindOfFile(w.file);
        if (w.kind == Kind::Unsupported) continue;
        w.preview = w.kind == Kind::Video ? QString() : w.file;
        out << w;
    }
    return out;
}

QString importFile(const QString &source, QString &error) {
    QFileInfo info(source);
    if (kindOfFile(source) == Kind::Unsupported) {
        error = info.fileName() + ": not an image, GIF or video";
        return QString();
    }
    QString target = paths::uploads() + "/" + info.fileName();
    // Never overwrite: "name (2).png", "name (3).png", ...
    for (int n = 2; QFileInfo::exists(target); ++n)
        target = paths::uploads() + "/" + info.completeBaseName() + " (" + QString::number(n) + ")." + info.suffix();
    if (!QFile::copy(source, target)) {
        error = info.fileName() + ": could not be copied";
        return QString();
    }
    return target;
}

bool wallpaperEngineInstalled() {
    for (const QString &lib : steamLibraries())
        if (QFile::exists(lib + "/steamapps/appmanifest_" + kWallpaperEngineApp + ".acf")) return true;
    return false;
}

QList<Wallpaper> scanWallpaperEngine() {
    QList<Wallpaper> out;
    for (const QString &lib : steamLibraries()) {
        QDir workshop(lib + "/steamapps/workshop/content/" + kWallpaperEngineApp);
        for (const QString &id : workshop.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString dir = workshop.filePath(id);
            QFile project(dir + "/project.json");
            if (!project.open(QIODevice::ReadOnly)) continue;
            QJsonObject json = QJsonDocument::fromJson(project.readAll()).object();
            Wallpaper w;
            w.id = id;
            w.title = json.value("title").toString(id);
            for (const QJsonValue &tag : json.value("tags").toArray()) w.tags << tag.toString();
            w.tags << json.value("description").toString();
            QString preview = json.value("preview").toString();
            if (!preview.isEmpty() && QFile::exists(dir + "/" + preview)) w.preview = dir + "/" + preview;
            QString type = json.value("type").toString().toLower();
            QString file = json.value("file").toString();
            if (type == "video" && !file.isEmpty() && QFile::exists(dir + "/" + file)) {
                w.file = dir + "/" + file;
                w.kind = kindOfFile(w.file);
            }
            if (w.kind == Kind::Unsupported) {
                w.file.clear();
                w.note = type.isEmpty() ? QString("Unknown wallpaper type")
                                        : "Wallpaper Engine \"" + type + "\" wallpapers are not supported yet";
            }
            out << w;
        }
    }
    return out;
}

QString videoThumbnail(const QString &video, bool create) {
    QFileInfo info(video);
    QByteArray key = (info.absoluteFilePath() + QString::number(info.lastModified().toSecsSinceEpoch())).toUtf8();
    QString thumb = paths::thumbs() + "/" + QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex() + ".jpg";
    if (QFile::exists(thumb)) return thumb;
    if (!create || QStandardPaths::findExecutable("ffmpeg").isEmpty()) return QString();
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-v", "quiet", "-y", "-ss", "1", "-i", video, "-frames:v", "1", "-vf", "scale=320:-2", thumb});
    if (!ffmpeg.waitForFinished(15000) || !QFile::exists(thumb)) {
        ffmpeg.kill();
        return QString();
    }
    return thumb;
}

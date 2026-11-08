// Wallpapers from links: a link straight to an image, GIF or video, or a web
// page with one in it (its og:video, <video> or og:image). The file is
// downloaded into a temporary folder; the caller then adds it to the library.
#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QStringList>
#include <QUrl>

#include <memory>

class QFile;
class QTemporaryDir;
class QNetworkReply;

// The http(s) links in pasted text (one per line, or separated by spaces).
QStringList linksInText(const QString &text);
// The pictures and videos a web page points at, best first: videos (og:video,
// <video>/<source>, links to video files), then pictures (og:image).
// Relative addresses are resolved against `base`.
QList<QUrl> mediaInPage(const QString &html, const QUrl &base);
// The extension for a downloaded file, from its first bytes (and, when those
// say nothing, its content type): "mp4", "webm", "gif", "png", ... or "" when
// it is not a picture or video roofce can show.
QString mediaExtension(const QByteArray &head, const QString &contentType);
// Whether an answer is a "browsers only" check page (Cloudflare and the like).
bool botCheckPage(int status, const QByteArray &head);

class LinkImporter : public QObject {
    Q_OBJECT
public:
    explicit LinkImporter(QObject *parent = nullptr);
    ~LinkImporter() override;
    // Downloads each link in turn.
    void add(const QStringList &links);
    // Stops the download and drops the links still waiting.
    void cancel();
    bool busy() const { return running_; }

signals:
    void progress(const QString &link, qint64 received, qint64 total);
    // `file` is the downloaded file (in a temporary folder, named after the
    // link), or "" with `error` set.
    void finished(const QString &link, const QString &file, const QString &error);
    void allDone();

private:
    void next();
    void fetch(const QUrl &url, bool fromPage);
    void done(const QString &file, const QString &error);

    QNetworkAccessManager net_;
    QQueue<QString> queue_;
    QString link_;              // the link being loaded, as given
    QNetworkReply *reply_ = nullptr;
    QFile *out_ = nullptr;
    QString incoming_;          // this window's temporary folder
    std::unique_ptr<QTemporaryDir> temp_;
    bool running_ = false;      // between the first link and allDone()
    bool cancelled_ = false;
    QString stop_;              // why the current download was stopped early
    enum { Unknown, Media, Page } kind_ = Unknown;
    QList<QUrl> candidates_;    // a page's pictures and videos still to try
};

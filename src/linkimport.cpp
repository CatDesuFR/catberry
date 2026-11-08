#include "linkimport.h"

#include "paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>

namespace {

const qint64 kMaxFile = 4LL << 30;  // 4 GB: a long 4K video at most
const qint64 kMaxPage = 8 << 20;    // web pages are read up to 8 MB

const QStringList kVideoExts = {"mp4", "webm", "mkv", "mov", "m4v", "avi"};
const QStringList kImageExts = {"gif", "png", "jpg", "jpeg", "webp", "bmp"};

// Mime types roofce can show, and the extension each file gets.
const QList<QPair<QString, QString>> kMediaTypes = {
    {"video/mp4", "mp4"},        {"video/webm", "webm"},     {"video/x-matroska", "mkv"},
    {"video/quicktime", "mov"},  {"video/x-m4v", "m4v"},     {"video/x-msvideo", "avi"},
    {"video/vnd.avi", "avi"},    {"image/gif", "gif"},       {"image/png", "png"},
    {"image/jpeg", "jpg"},       {"image/webp", "webp"},     {"image/bmp", "bmp"},
};

QString extensionFor(const QString &mime) {
    for (const auto &type : kMediaTypes)
        if (mime == type.first) return type.second;
    return QString();
}

QString decodeEntities(QString text) {
    text.replace("&amp;", "&").replace("&quot;", "\"").replace("&#39;", "'").replace("&#x27;", "'");
    text.replace("&lt;", "<").replace("&gt;", ">").replace("&#x2F;", "/").replace("&#47;", "/");
    return text.trimmed();
}

// A file name from the link: its last path part, without odd characters.
QString nameFromUrl(const QUrl &url) {
    QString name = QFileInfo(url.path()).completeBaseName();
    name.replace(QRegularExpression("[^A-Za-z0-9 ._()-]+"), "-");
    name = name.left(80).trimmed();
    while (name.startsWith('.') || name.startsWith('-')) name.remove(0, 1);
    return name.isEmpty() ? url.host().section('.', -2, -2) + "-wallpaper" : name;
}

} // namespace

QStringList linksInText(const QString &text) {
    QStringList links;
    for (const QString &word : text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
        QUrl url(word.trimmed(), QUrl::StrictMode);
        if (url.isValid() && (url.scheme() == "http" || url.scheme() == "https") && !url.host().isEmpty() &&
            !links.contains(word.trimmed()))
            links << word.trimmed();
    }
    return links;
}

QList<QUrl> mediaInPage(const QString &html, const QUrl &base) {
    QList<QUrl> videos, pictures;
    QSet<QString> seen;
    auto add = [&](QList<QUrl> &list, const QString &address) {
        QString text = decodeEntities(address);
        if (text.isEmpty() || text.startsWith("data:") || text.startsWith("blob:")) return;
        QUrl url = base.resolved(QUrl(text));
        if ((url.scheme() != "http" && url.scheme() != "https") || seen.contains(url.toString())) return;
        seen.insert(url.toString());
        list << url;
    };
    static const QRegularExpression attr(R"re(([a-zA-Z:_-]+)\s*=\s*("([^"]*)"|'([^']*)'))re");
    auto attributes = [](const QString &tag) {
        QMap<QString, QString> map;
        auto it = attr.globalMatch(tag);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            map.insert(m.captured(1).toLower(), m.captured(3).isNull() ? m.captured(4) : m.captured(3));
        }
        return map;
    };

    // <meta property="og:video" content="..."> and friends, in any attribute order.
    static const QRegularExpression meta(R"re(<meta\b[^>]*>)re", QRegularExpression::CaseInsensitiveOption);
    QList<QPair<int, QString>> metaVideos, metaPictures; // (rank, address)
    const QStringList videoKeys = {"og:video:secure_url", "og:video:url", "og:video", "twitter:player:stream"};
    const QStringList pictureKeys = {"og:image:secure_url", "og:image:url", "og:image", "twitter:image"};
    auto metas = meta.globalMatch(html);
    while (metas.hasNext()) {
        auto a = attributes(metas.next().captured(0));
        QString key = (a.value("property").isEmpty() ? a.value("name") : a.value("property")).toLower();
        QString content = a.value("content");
        if (videoKeys.contains(key)) metaVideos << qMakePair(int(videoKeys.indexOf(key)), content);
        if (pictureKeys.contains(key)) metaPictures << qMakePair(int(pictureKeys.indexOf(key)), content);
    }
    std::stable_sort(metaVideos.begin(), metaVideos.end(), [](auto &x, auto &y) { return x.first < y.first; });
    std::stable_sort(metaPictures.begin(), metaPictures.end(), [](auto &x, auto &y) { return x.first < y.first; });
    for (const auto &v : metaVideos) add(videos, v.second);

    // <video src>, <source src> and <video poster> (a picture).
    static const QRegularExpression media(R"re(<(video|source)\b[^>]*>)re", QRegularExpression::CaseInsensitiveOption);
    // When a page offers several sizes (data-height="1080", res="720",
    // size="480", label="1080p"...), the biggest comes first.
    auto tags = media.globalMatch(html);
    QStringList posters;
    QList<QPair<int, QString>> sources;
    static const QRegularExpression digits("(\\d{3,4})");
    while (tags.hasNext()) {
        auto a = attributes(tags.next().captured(0));
        int height = 0;
        for (const char *key : {"data-height", "height", "res", "size", "label", "data-res", "data-quality"})
            height = qMax(height, digits.match(a.value(key)).captured(1).toInt());
        if (!a.value("src").isEmpty()) sources << qMakePair(height, a.value("src"));
        posters << a.value("poster");
    }
    std::stable_sort(sources.begin(), sources.end(), [](auto &x, auto &y) { return x.first > y.first; });
    for (const auto &source : sources) add(videos, source.second);
    // Plain links to video files anywhere in the page (scripts included).
    static const QRegularExpression file(R"re((https?:)?//[^\s"'<>()\\]+?\.(mp4|webm|mov|m4v|mkv)(\?[^\s"'<>()\\]*)?(?=["'\s<>)\\]))re",
                                         QRegularExpression::CaseInsensitiveOption);
    // Scripts often write slashes as "\/".
    QString unescaped = QString(html).replace("\\/", "/");
    auto files = file.globalMatch(unescaped);
    while (files.hasNext()) add(videos, files.next().captured(0));

    for (const auto &p : metaPictures) add(pictures, p.second);
    for (const QString &p : posters) add(pictures, p);
    return videos + pictures;
}

QString mediaExtension(const QByteArray &head, const QString &contentType) {
    QMimeDatabase db;
    QMimeType sniffed = db.mimeTypeForData(head);
    QString ext = extensionFor(sniffed.name());
    if (!ext.isEmpty()) return ext;
    // Text (a page, an error message) is never a wallpaper, whatever the server says.
    if (sniffed.inherits("text/plain")) return QString();
    // The bytes say nothing roofce knows: trust the server only for types
    // whose start is hard to recognise, never for text or HTML.
    QString type = contentType.section(';', 0, 0).trimmed().toLower();
    if (head.isEmpty() || type.startsWith("text/")) return QString();
    QString fromType = extensionFor(type);
    return fromType;
}

bool botCheckPage(int status, const QByteArray &head) {
    if (status != 403 && status != 429 && status != 503) return false;
    QByteArray lower = head.toLower();
    for (const char *mark : {"cf_challenge", "cf-chl", "challenge-platform", "just a moment", "captcha",
                             "checking your browser", "attention required"})
        if (lower.contains(mark)) return true;
    return false;
}

LinkImporter::LinkImporter(QObject *parent) : QObject(parent) {
    incoming_ = QDir::cleanPath(paths::thumbs() + "/../incoming");
    QDir(incoming_).removeRecursively(); // leftovers of an earlier run
    QDir().mkpath(incoming_);
}

void LinkImporter::add(const QStringList &links) {
    for (const QString &link : links) queue_.enqueue(link);
    if (!busy()) next();
}

void LinkImporter::next() {
    if (queue_.isEmpty()) {
        emit allDone();
        return;
    }
    link_ = queue_.dequeue();
    fetch(QUrl(link_), false);
}

void LinkImporter::fetch(const QUrl &url, bool fromPage) {
    if (!fromPage) candidates_.clear();
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")) {
        done(QString(), "Only http and https links work");
        return;
    }
    // ArtStation's artwork pages load their videos with scripts, from data
    // only browsers may read: the page itself holds just a cover picture.
    if (!fromPage && (url.host() == "artstation.com" || url.host().endsWith(".artstation.com")) &&
        url.path().startsWith("/artwork/")) {
        done(QString(), "ArtStation pages show their videos only in a browser. Open the page, then drag the video "
                        "onto roofce, or right-click it, copy the video address and paste that here.");
        return;
    }
    QDir().mkpath(incoming_);
    out_ = new QFile(incoming_ + "/download.part", this);
    if (!out_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        done(QString(), "Could not save the download");
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "catberry-roofce/" ROOFCE_VERSION " (https://github.com/CatDesuFR/catberry)");
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    reply_ = net_.get(request);
    reply_->setReadBufferSize(1 << 20);
    QNetworkReply *reply = reply_;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        out_->write(reply->readAll());
        if (out_->size() > kMaxFile) reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit progress(link_, received, total); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, fromPage] {
        reply->deleteLater();
        reply_ = nullptr;
        out_->write(reply->readAll());
        out_->close();
        qint64 size = out_->size();
        QByteArray head;
        if (out_->open(QIODevice::ReadOnly)) {
            head = out_->read(64 * 1024);
            out_->close();
        }
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
        auto fail = [&](const QString &error) {
            out_->remove();
            delete out_;
            out_ = nullptr;
            // From a page: try its next picture or video.
            if (fromPage && !candidates_.isEmpty()) {
                fetch(candidates_.takeFirst(), true);
                return;
            }
            done(QString(), error);
        };
        if (size > kMaxFile) {
            fail("Bigger than 4 GB");
            return;
        }
        if (botCheckPage(status, head)) {
            fail("That site only lets web browsers in. Open the page in your browser, then drag the video (or "
                 "picture) onto roofce, or right-click it, copy its address and paste that here.");
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            fail("Could not load it: " + reply->errorString());
            return;
        }
        QString ext = mediaExtension(head, type);
        if (!ext.isEmpty()) {
            QString target = incoming_ + "/" + nameFromUrl(reply->url()) + "." + ext;
            QFile::remove(target);
            if (!out_->rename(target)) {
                fail("Could not save the download");
                return;
            }
            delete out_;
            out_ = nullptr;
            done(target, QString());
            return;
        }
        bool page = type.contains("html") || head.trimmed().startsWith('<');
        if (page && !fromPage && size <= kMaxPage && out_->open(QIODevice::ReadOnly)) {
            QString html = QString::fromUtf8(out_->readAll());
            out_->close();
            candidates_ = mediaInPage(html, reply->url());
            if (candidates_.isEmpty()) {
                fail("No picture or video found on that page");
                return;
            }
            out_->remove();
            delete out_;
            out_ = nullptr;
            fetch(candidates_.takeFirst(), true);
            return;
        }
        fail(fromPage ? "The page's pictures and videos could not be loaded"
                      : "That link is not a picture, GIF or video");
    });
}

void LinkImporter::done(const QString &file, const QString &error) {
    if (out_) {
        out_->remove();
        delete out_;
        out_ = nullptr;
    }
    emit finished(link_, file, error);
    next();
}

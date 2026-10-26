#include "online.h"

#include "paths.h"

#include <QFile>
#include <QSharedPointer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>

namespace {

const int kPageSize = 24;

// Words that mark a wallpaper (or a search) as not suitable. Checked as
// whole words against titles and searches from every source.
const QStringList kBlocked = {
    "nude",     "nudes",    "nudity",   "naked",  "nsfw",      "sex",       "sexy",      "erotic",
    "erotica",  "porn",     "lingerie", "bikini", "bikinis",   "underwear", "swimsuit",  "topless",
    "boobs",    "breast",   "breasts",  "butt",   "cleavage",  "panties",   "ecchi",     "hentai",
    "fetish",   "seductive", "sensual", "provocative", "bra", "thong",    "stripper",  "playboy",
};

// Asks Commons' search to leave these out as well.
const char *const kCommonsExclude = " -nude -naked -nudity -erotic -sexy -lingerie -bikini -topless";

QString https(QString url) {
    if (url.startsWith("http://")) url.replace(0, 7, "https://");
    if (url.startsWith("//")) url.prepend("https:");
    return url;
}

struct GenreInfo {
    const char *name;
    const char *words;      // added to searches
    const char *motionTag;  // MotionBgs genre page
    bool commons, nasa;     // whether those sources have it at all
};
const GenreInfo kGenres[kGenreCount] = {
    {"Any genre", "", "", true, true},
    {"Scenery", "landscape", "nature", true, true},
    {"Space", "space", "space", true, true},
    {"Anime", "anime", "anime", false, false},
    {"Games", "video game", "games", false, false},
    {"Cars", "car", "car", true, false},
    {"Animals", "animal", "animal", true, false},
    {"City", "city", "city", true, false},
    {"Fantasy", "fantasy", "fantasy", false, false},
    {"Abstract", "abstract", "abstract", true, false},
};

QString humanResolution(int width, int height) {
    return width > 0 && height > 0 ? QString("%1x%2").arg(width).arg(height) : QString();
}

} // namespace

QString genreName(Genre genre) { return kGenres[int(genre)].name; }

QString OnlineWallpaper::key() const { return OnlineLibrary::sourceName(source).toLower() + "-" + id; }

OnlineLibrary::OnlineLibrary(QObject *parent) : QObject(parent) {}

QString OnlineLibrary::sourceName(OnlineSource source) {
    switch (source) {
    case OnlineSource::Wallhaven: return "Wallhaven";
    case OnlineSource::MotionBgs: return "MotionBgs";
    case OnlineSource::Commons: return "Wikimedia";
    case OnlineSource::Nasa: return "NASA";
    }
    return QString();
}

bool OnlineLibrary::blocked(const QString &text) {
    static const QRegularExpression words("[^a-z]+");
    for (const QString &word : text.toLower().split(words, Qt::SkipEmptyParts))
        if (kBlocked.contains(word)) return true;
    return false;
}

QNetworkReply *OnlineLibrary::get(const QString &url) {
    QNetworkRequest request{QUrl(url)};
    // Wikimedia asks every client to identify itself.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "catberry-roofce/" ROOFCE_VERSION " (https://github.com/CatDesuFR/catberry)");
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return net_.get(request);
}

void OnlineLibrary::search(const QString &query, OnlineFilter filter, Genre genre, const QList<OnlineSource> &sources,
                           bool more) {
    if (!more) {
        query_ = query;
        filter_ = filter;
        genre_ = genre;
        sources_ = sources;
        page_.clear();
        hasMore_.clear();
    }
    int generation = ++generation_;
    pending_ = 0;
    for (OnlineSource source : sources_) {
        if (more && !hasMore_.value(source)) continue;
        // Wallhaven only has pictures, MotionBgs only live wallpapers, and
        // Commons and NASA have nothing for some genres.
        if (source == OnlineSource::Wallhaven && filter_ == OnlineFilter::Live) continue;
        if (source == OnlineSource::MotionBgs && filter_ == OnlineFilter::Pictures) continue;
        if (source == OnlineSource::Commons && !kGenres[int(genre_)].commons) continue;
        if (source == OnlineSource::Nasa && !kGenres[int(genre_)].nasa) continue;
        ++pending_;
        switch (source) {
        case OnlineSource::Wallhaven: searchWallhaven(generation); break;
        case OnlineSource::MotionBgs: searchMotionBgs(generation); break;
        case OnlineSource::Commons: searchCommons(generation); break;
        case OnlineSource::Nasa: searchNasa(generation); break;
        }
    }
    if (pending_ == 0) emit searchFinished(false);
}

void OnlineLibrary::sourceDone(int generation, OnlineSource source, bool more) {
    if (generation != generation_) return;
    hasMore_[source] = more;
    if (--pending_ > 0) return;
    bool any = false;
    for (bool value : hasMore_) any |= value;
    emit searchFinished(any);
}

void OnlineLibrary::deliver(int generation, QList<OnlineWallpaper> items) {
    if (generation != generation_) return;
    items.erase(std::remove_if(items.begin(), items.end(), [](const OnlineWallpaper &w) { return blocked(w.title); }),
                items.end());
    if (!items.isEmpty()) emit results(items);
}

// --- Wallhaven: pictures ------------------------------------------------------

void OnlineLibrary::searchWallhaven(int generation) {
    int page = page_.value(OnlineSource::Wallhaven, 1);
    QUrlQuery params;
    QString terms = (query_ + " " + kGenres[int(genre_)].words).trimmed();
    QString q = terms;
    if (genre_ == Genre::Anime) q += " -ecchi -bikini -swimsuit -lingerie -underwear -cleavage -panties";
    if (!q.isEmpty()) params.addQueryItem("q", q);
    // "General" only, except for the anime genre: Wallhaven's anime and
    // people categories are where suggestive pictures turn up even among its
    // "safe" ones. People stays off; for anime, its tags for such pictures
    // are excluded on top.
    params.addQueryItem("categories", genre_ == Genre::Anime ? "010" : "100");
    params.addQueryItem("purity", "100");
    params.addQueryItem("sorting", terms.isEmpty() ? "toplist" : "relevance");
    params.addQueryItem("topRange", "1y");
    params.addQueryItem("atleast", "1280x720");
    params.addQueryItem("page", QString::number(page));
    QUrl url("https://wallhaven.cc/api/v1/search");
    url.setQuery(params);

    QNetworkReply *reply = get(url.toString());
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, page] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (generation == generation_) emit failed("Wallhaven: " + reply->errorString());
            sourceDone(generation, OnlineSource::Wallhaven, false);
            return;
        }
        QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        QList<OnlineWallpaper> items;
        for (const QJsonValue &value : json.value("data").toArray()) {
            QJsonObject o = value.toObject();
            OnlineWallpaper w;
            w.source = OnlineSource::Wallhaven;
            w.id = o.value("id").toString();
            w.title = "Wallhaven " + w.id;
            // "original" is the uncropped preview; "small" is cut to 3:2.
            w.thumbUrl = o.value("thumbs").toObject().value("original").toString();
            w.previewUrl = w.thumbUrl;
            w.fullUrl = o.value("path").toString();
            w.resolution = o.value("resolution").toString();
            w.fileSize = o.value("file_size").toVariant().toLongLong();
            if (!w.id.isEmpty() && !w.fullUrl.isEmpty()) items << w;
        }
        page_[OnlineSource::Wallhaven] = page + 1;
        deliver(generation, items);
        int lastPage = json.value("meta").toObject().value("last_page").toInt(page);
        sourceDone(generation, OnlineSource::Wallhaven, page < lastPage);
    });
}

// --- Wikimedia Commons: videos and pictures -----------------------------------

void OnlineLibrary::searchCommons(int generation) {
    int offset = page_.value(OnlineSource::Commons, 0);
    QString terms = (query_ + " " + kGenres[int(genre_)].words).trimmed();
    if (terms.isEmpty()) terms = filter_ == OnlineFilter::Pictures ? "landscape" : "timelapse";
    if (filter_ == OnlineFilter::Live)
        terms += " filetype:video";
    else if (filter_ == OnlineFilter::Pictures)
        terms += " filetype:bitmap";
    else
        terms += " filetype:video|bitmap";
    // Wide enough to fill a screen.
    terms += " filew:>1279";
    terms += kCommonsExclude;

    QUrlQuery params;
    params.addQueryItem("action", "query");
    params.addQueryItem("format", "json");
    params.addQueryItem("generator", "search");
    params.addQueryItem("gsrnamespace", "6"); // files
    params.addQueryItem("gsrsearch", terms);
    params.addQueryItem("gsrlimit", QString::number(kPageSize));
    params.addQueryItem("gsroffset", QString::number(offset));
    params.addQueryItem("prop", "videoinfo");
    params.addQueryItem("viprop", "url|size|mime|derivatives");
    params.addQueryItem("viurlwidth", "480");
    QUrl url("https://commons.wikimedia.org/w/api.php");
    url.setQuery(params);

    QNetworkReply *reply = get(url.toString());
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, offset] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (generation == generation_) emit failed("Wikimedia Commons: " + reply->errorString());
            sourceDone(generation, OnlineSource::Commons, false);
            return;
        }
        QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        QList<OnlineWallpaper> items;
        QJsonObject pages = json.value("query").toObject().value("pages").toObject();
        for (const QJsonValue &value : pages) {
            QJsonObject page = value.toObject();
            QJsonObject info = page.value("videoinfo").toArray().at(0).toObject();
            QString mime = info.value("mime").toString();
            OnlineWallpaper w;
            w.source = OnlineSource::Commons;
            w.id = QString::number(page.value("pageid").toInteger());
            w.title = page.value("title").toString().remove(QRegularExpression("^File:")).section('.', 0, -2);
            w.video = mime.startsWith("video/") || mime == "application/ogg";
            w.thumbUrl = info.value("thumburl").toString();
            w.fullUrl = info.value("url").toString();
            w.fileSize = info.value("size").toVariant().toLongLong();
            int width = info.value("width").toInt(), height = info.value("height").toInt();
            w.resolution = humanResolution(width, height);
            w.previewUrl = w.video ? QString() : w.thumbUrl;
            if (w.video) {
                // Commons keeps smaller copies of every video: a small one
                // to stream while hovering, and at most 1080p to download.
                int bestPreview = 0, bestFull = 0;
                for (const QJsonValue &d : info.value("derivatives").toArray()) {
                    QJsonObject o = d.toObject();
                    int h = o.value("height").toInt();
                    QString src = https(o.value("src").toString());
                    if (!o.value("type").toString().startsWith("video/") || src.isEmpty()) continue;
                    if (h <= 480 && h > bestPreview) {
                        bestPreview = h;
                        w.previewUrl = src;
                    }
                    if (h <= 1080 && h > bestFull && height > 1080) {
                        bestFull = h;
                        w.fullUrl = src;
                        w.fileSize = 0; // the copy's size is not listed
                        w.resolution = humanResolution(width * h / qMax(1, height), h);
                    }
                }
                if (w.previewUrl.isEmpty()) w.previewUrl = w.fullUrl;
            }
            if (!w.fullUrl.isEmpty() && !w.thumbUrl.isEmpty()) items << w;
        }
        int next = json.value("continue").toObject().value("gsroffset").toInt(0);
        page_[OnlineSource::Commons] = next > offset ? next : offset + kPageSize;
        deliver(generation, items);
        sourceDone(generation, OnlineSource::Commons, next > offset);
    });
}

// --- MotionBgs: live wallpapers -------------------------------------------------

void OnlineLibrary::searchMotionBgs(int generation) {
    int page = page_.value(OnlineSource::MotionBgs, 1);
    QString tag = kGenres[int(genre_)].motionTag;
    QUrl url;
    if (!query_.isEmpty()) {
        // Its search sends single words that are also genres to the genre
        // page; following redirects handles that.
        QUrlQuery params;
        params.addQueryItem("q", (query_ + " " + kGenres[int(genre_)].words).trimmed());
        if (page > 1) params.addQueryItem("page", QString::number(page));
        url = QUrl("https://motionbgs.com/search");
        url.setQuery(params);
    } else if (!tag.isEmpty()) {
        url = QUrl("https://motionbgs.com/tag:" + tag + "/" + (page > 1 ? QString::number(page) + "/" : ""));
    } else {
        url = QUrl("https://motionbgs.com/" + (page > 1 ? QString::number(page) + "/" : ""));
    }

    QNetworkReply *reply = get(url.toString());
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, page] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // Past the last page the site answers "not found".
            if (generation == generation_ && page == 1) emit failed("MotionBgs: " + reply->errorString());
            sourceDone(generation, OnlineSource::MotionBgs, false);
            return;
        }
        // No API: read the wallpaper tiles from the page, one at a time so
        // a title is never paired with the next tile's video. Each is
        //   <a title="NAME live wallpaper" href=/SLUG> ... src=/i/c/364x205/media/ID/FILE[.WxH].jpg
        QString html = QString::fromUtf8(reply->readAll());
        static const QRegularExpression head("^([^\"]*?) live wallpaper\" href=/([a-z0-9-]+)>");
        static const QRegularExpression image("src=/i/c/364x205/media/(\\d+)/([^ >]+?)(\\.\\d+x\\d+)?\\.jpg");
        QList<OnlineWallpaper> items;
        QStringList tiles = html.split("<a title=\"");
        for (int i = 1; i < tiles.size(); ++i) {
            auto h = head.match(tiles[i]);
            auto m = image.match(tiles[i]);
            if (!h.hasMatch() || !m.hasMatch()) continue;
            OnlineWallpaper w;
            w.source = OnlineSource::MotionBgs;
            w.id = m.captured(1);
            w.title = h.captured(1);
            w.video = true;
            QString base = m.captured(2);
            w.thumbUrl = "https://motionbgs.com/i/c/364x205/media/" + w.id + "/" + base + m.captured(3) + ".jpg";
            w.previewUrl = "https://motionbgs.com/media/" + w.id + "/" + base + ".960x540.mp4";
            // The 1080p copy rather than 4K: a few MB instead of tens.
            w.fullUrl = "https://motionbgs.com/dl/hd/" + w.id + "/";
            w.resolution = "1920x1080";
            items << w;
        }
        page_[OnlineSource::MotionBgs] = page + 1;
        deliver(generation, items);
        // A full page means there may be another.
        sourceDone(generation, OnlineSource::MotionBgs, items.size() >= 20);
    });
}

// --- NASA: videos and pictures --------------------------------------------------

void OnlineLibrary::searchNasa(int generation) {
    int page = page_.value(OnlineSource::Nasa, 1);
    QUrlQuery params;
    QString terms = query_.isEmpty() ? QString(genre_ == Genre::Scenery ? "earth" : "earth from space") : query_;
    params.addQueryItem("q", terms);
    params.addQueryItem("media_type", filter_ == OnlineFilter::Live       ? "video"
                                      : filter_ == OnlineFilter::Pictures ? "image"
                                                                          : "video,image");
    params.addQueryItem("page", QString::number(page));
    params.addQueryItem("page_size", QString::number(kPageSize));
    QUrl url("https://images-api.nasa.gov/search");
    url.setQuery(params);

    QNetworkReply *reply = get(url.toString());
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, page] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (generation == generation_) emit failed("NASA: " + reply->errorString());
            sourceDone(generation, OnlineSource::Nasa, false);
            return;
        }
        QJsonObject collection = QJsonDocument::fromJson(reply->readAll()).object().value("collection").toObject();
        QList<OnlineWallpaper> items;
        for (const QJsonValue &value : collection.value("items").toArray()) {
            QJsonObject item = value.toObject();
            QJsonObject data = item.value("data").toArray().at(0).toObject();
            OnlineWallpaper w;
            w.source = OnlineSource::Nasa;
            w.id = data.value("nasa_id").toString();
            w.title = data.value("title").toString();
            w.video = data.value("media_type").toString() == "video";
            for (const QJsonValue &link : item.value("links").toArray())
                if (link.toObject().value("rel").toString() == "preview")
                    w.thumbUrl = https(link.toObject().value("href").toString());
            w.assetsUrl = https(item.value("href").toString());
            if (!w.id.isEmpty() && !w.thumbUrl.isEmpty() && !w.assetsUrl.isEmpty()) items << w;
        }
        bool more = false;
        for (const QJsonValue &link : collection.value("links").toArray())
            more |= link.toObject().value("rel").toString() == "next";
        page_[OnlineSource::Nasa] = page + 1;
        deliver(generation, items);
        sourceDone(generation, OnlineSource::Nasa, more);
    });
}

void OnlineLibrary::resolve(const OnlineWallpaper &w) {
    if (!w.previewUrl.isEmpty() && !w.fullUrl.isEmpty()) {
        emit resolved(w.key(), w.previewUrl, w.fullUrl);
        return;
    }
    if (w.assetsUrl.isEmpty()) return;
    QNetworkReply *reply = get(w.assetsUrl);
    QString key = w.key();
    bool video = w.video;
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, video] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        // A list of every copy NASA keeps: ~orig, ~large, ~medium, ~small,
        // ~mobile... Stream a small one; download a large (not original,
        // which can be huge) one.
        QStringList files;
        for (const QJsonValue &value : QJsonDocument::fromJson(reply->readAll()).array())
            files << https(value.toString());
        QString ext = video ? ".mp4" : ".jpg";
        auto pick = [&](const QStringList &names) {
            for (const QString &name : names)
                for (const QString &file : files)
                    if (file.endsWith(name + ext, Qt::CaseInsensitive)) return file;
            return QString();
        };
        QString preview = video ? pick({"~mobile", "~small", "~preview", "~medium", "~large", "~orig"})
                                : pick({"~medium", "~small", "~large", "~orig"});
        QString full = pick({"~large", "~orig", "~medium"});
        if (!preview.isEmpty() && !full.isEmpty()) emit resolved(key, preview, full);
    });
}

// --- previews and downloads -----------------------------------------------------

void OnlineLibrary::fetchThumb(const OnlineWallpaper &w) {
    QString path = paths::thumbs() + "/" + w.key() + ".jpg";
    if (QFile::exists(path)) {
        emit thumbReady(w.key(), path);
        return;
    }
    QNetworkReply *reply = get(w.thumbUrl);
    QString key = w.key();
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, path] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QFile out(path);
        if (out.open(QIODevice::WriteOnly) && out.write(reply->readAll()) > 0) {
            out.close();
            emit thumbReady(key, path);
        }
    });
}

QString OnlineLibrary::localFile(const OnlineWallpaper &w) {
    QString suffix = QFileInfo(QUrl(w.fullUrl).path()).suffix();
    if (suffix.isEmpty()) suffix = w.video ? "mp4" : "jpg";
    return paths::downloads() + "/" + w.key() + "." + suffix;
}

void OnlineLibrary::download(const OnlineWallpaper &w) {
    QString path = localFile(w);
    if (QFile::exists(path)) {
        emit downloaded(w.key(), path);
        return;
    }
    // Videos can be hundreds of MB: write them out as they arrive instead
    // of holding them in memory. The temporary name means a broken
    // download never looks like a finished one.
    auto out = QSharedPointer<QFile>::create(path + ".part");
    if (!out->open(QIODevice::WriteOnly)) {
        emit failed("Could not save the wallpaper");
        return;
    }
    QNetworkReply *reply = get(w.fullUrl);
    reply->setReadBufferSize(1 << 20);
    QString key = w.key();
    connect(reply, &QNetworkReply::readyRead, this, [reply, out] { out->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, key](qint64 received, qint64 total) { emit downloadProgress(key, received, total); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, path, out] {
        reply->deleteLater();
        out->write(reply->readAll());
        out->close();
        if (reply->error() != QNetworkReply::NoError || out->size() == 0) {
            out->remove();
            emit failed("Download failed: " + reply->errorString());
            return;
        }
        QFile::remove(path);
        QFile::rename(path + ".part", path);
        emit downloaded(key, path);
    });
}

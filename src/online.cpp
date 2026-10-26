#include "online.h"

#include "paths.h"

#include <QDateTime>
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
    "waifu",    "lewd",     "thicc",    "booty",  "busty",     "nipple",    "nipples",   "undressed",
    "undress",  "pinup",    "seduction", "bathing", "onsen",   "swimwear",  "succubus",  "nikke",
};

// Signs of AI-generated art, in titles and Commons categories.
const QStringList kAiWords = {"ai", "aiart", "midjourney", "dall", "dalle", "diffusion", "generated", "generative",
                              "aigc", "novelai", "nijijourney"};

// Commons categories that mark pictures as not suitable (checked as
// substrings, so "Nude women in art" and "Topless women" both count).
const QStringList kBlockedCategories = {"nude", "nudity", "naked", "erotic", "topless", "lingerie", "bikini",
                                        "swimwear", "breast", "sexual", "pornograph", "underwear", "buttocks"};

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

QString resolutionName(Resolution r) {
    switch (r) {
    case Resolution::Any: return "Any resolution";
    case Resolution::P1080: return "1080p";
    case Resolution::K2: return "2K";
    case Resolution::K4: return "4K";
    case Resolution::K8: return "8K";
    }
    return QString();
}

Resolution resolutionClass(int width, int height) {
    int side = qMax(width, height);
    if (side >= 7680) return Resolution::K8;
    if (side >= 3840) return Resolution::K4;
    if (side >= 2560) return Resolution::K2;
    if (side >= 1920) return Resolution::P1080;
    return Resolution::Any;
}

Resolution resolutionClass(const QString &resolution) {
    QStringList parts = resolution.split('x');
    return parts.size() == 2 ? resolutionClass(parts[0].toInt(), parts[1].toInt()) : Resolution::Any;
}

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

bool OnlineLibrary::aiMade(const QString &text) {
    QString lower = text.toLower();
    if (lower.contains("ai-generated") || lower.contains("ai generated") || lower.contains("artificial intelligence") ||
        lower.contains("stable diffusion"))
        return true;
    static const QRegularExpression words("[^a-z]+");
    QStringList list = lower.split(words, Qt::SkipEmptyParts);
    // "generated" alone is too common; only with "ai" nearby.
    for (const QString &word : list)
        if (word != "generated" && word != "generative" && word != "diffusion" && kAiWords.contains(word)) return true;
    return false;
}

QStringList OnlineLibrary::variants(const QString &word) {
    QString w = word.toLower();
    QStringList out = {w};
    if (w.size() < 3) return out;
    if (w.endsWith("ies"))
        out << w.chopped(3) + "y";
    else if (w.endsWith("es") && (w.endsWith("ches") || w.endsWith("shes") || w.endsWith("xes") || w.endsWith("sses")))
        out << w.chopped(2);
    else if (w.endsWith('s') && !w.endsWith("ss"))
        out << w.chopped(1);
    else if (w.endsWith('y') && !QString("aeiou").contains(w.at(w.size() - 2)))
        out << w.chopped(1) + "ies";
    else
        out << w + "s";
    return out;
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

void OnlineLibrary::search(const SearchOptions &options, bool more) {
    if (!more) {
        options_ = options;
        page_.clear();
        hasMore_.clear();
        seen_.clear();
    }
    int generation = ++generation_;
    pending_ = 0;
    for (OnlineSource source : options_.sources) {
        if (more && !hasMore_.value(source)) continue;
        // Wallhaven only has pictures, MotionBgs only live wallpapers, and
        // Commons and NASA have nothing for some genres.
        if (source == OnlineSource::Wallhaven && options_.filter == OnlineFilter::Live) continue;
        if (source == OnlineSource::MotionBgs && options_.filter == OnlineFilter::Pictures) continue;
        if (source == OnlineSource::Commons && !kGenres[int(options_.genre)].commons) continue;
        if (source == OnlineSource::Nasa && !kGenres[int(options_.genre)].nasa) continue;
        // NASA does not say how big its files are; MotionBgs only has 1080p and 4K.
        if (source == OnlineSource::Nasa && options_.resolution != Resolution::Any) continue;
        if (source == OnlineSource::MotionBgs &&
            (options_.resolution == Resolution::K2 || options_.resolution == Resolution::K8))
            continue;
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
    QList<OnlineWallpaper> shown;
    for (const OnlineWallpaper &w : items) {
        // The same wallpaper can come from a search and a genre page.
        if (seen_.contains(w.key()) || blocked(w.title)) continue;
        if (options_.resolution != Resolution::Any && resolutionClass(w.resolution) != options_.resolution) continue;
        QString tags = w.tags.join(' ');
        if (options_.hideAi && aiMade(w.title + " " + tags)) continue;
        if (options_.strict) {
            QString lower = tags.toLower();
            bool bad = std::any_of(kBlockedCategories.begin(), kBlockedCategories.end(),
                                   [&lower](const QString &c) { return lower.contains(c); });
            if (bad || (w.source == OnlineSource::MotionBgs && motionFiltered_.contains(w.id))) continue;
        }
        seen_.insert(w.key());
        shown << w;
    }
    if (!shown.isEmpty()) emit results(shown);
}

// --- Wallhaven: pictures ------------------------------------------------------

void OnlineLibrary::searchWallhaven(int generation) {
    int page = page_.value(OnlineSource::Wallhaven, 1);
    QUrlQuery params;
    QString terms = (options_.query + " " + kGenres[int(options_.genre)].words).trimmed();
    QString q = terms;
    if (options_.genre == Genre::Anime) q += " -ecchi -bikini -swimsuit -lingerie -underwear -cleavage -panties";
    if (!q.isEmpty()) params.addQueryItem("q", q);
    // "General" only, except for the anime genre: Wallhaven's anime and
    // people categories are where suggestive pictures turn up even among its
    // "safe" ones. People stays off; for anime, its tags for such pictures
    // are excluded on top.
    params.addQueryItem("categories", options_.genre == Genre::Anime ? "010" : "100");
    params.addQueryItem("purity", "100");
    params.addQueryItem("sorting", terms.isEmpty() ? "toplist" : "relevance");
    params.addQueryItem("topRange", "1y");
    // Every size by default; with a size class, at least its smallest
    // (bigger ones are dropped when the results come in).
    static const char *const atLeast[] = {"", "1920x1080", "2560x1440", "3840x2160", "7680x4320"};
    if (options_.resolution != Resolution::Any) params.addQueryItem("atleast", atLeast[int(options_.resolution)]);
    if (options_.hideAi) params.addQueryItem("ai_art_filter", "1");
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
    QString terms = (options_.query + " " + kGenres[int(options_.genre)].words).trimmed();
    if (terms.isEmpty()) terms = options_.filter == OnlineFilter::Pictures ? "landscape" : "timelapse";
    if (options_.filter == OnlineFilter::Live)
        terms += " filetype:video";
    else if (options_.filter == OnlineFilter::Pictures)
        terms += " filetype:bitmap";
    else
        terms += " filetype:video|bitmap";
    static const int minWidth[] = {0, 1920, 2560, 3840, 7680};
    if (options_.resolution != Resolution::Any)
        terms += " filew:>" + QString::number(minWidth[int(options_.resolution)] - 1);
    terms += kCommonsExclude;
    if (options_.hideAi) terms += " -\"AI-generated\" -midjourney -\"stable diffusion\"";

    QUrlQuery params;
    params.addQueryItem("action", "query");
    params.addQueryItem("format", "json");
    params.addQueryItem("generator", "search");
    params.addQueryItem("gsrnamespace", "6"); // files
    params.addQueryItem("gsrsearch", terms);
    params.addQueryItem("gsrlimit", QString::number(kPageSize));
    params.addQueryItem("gsroffset", QString::number(offset));
    params.addQueryItem("prop", "videoinfo|categories");
    params.addQueryItem("clshow", "!hidden");
    params.addQueryItem("cllimit", "max");
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
            for (const QJsonValue &c : page.value("categories").toArray())
                w.tags << c.toObject().value("title").toString().remove(QRegularExpression("^Category:"));
            w.fullUrl = info.value("url").toString();
            w.fileSize = info.value("size").toVariant().toLongLong();
            int width = info.value("width").toInt(), height = info.value("height").toInt();
            w.resolution = humanResolution(width, height);
            w.previewUrl = w.video ? QString() : w.thumbUrl;
            if (w.video) {
                // Commons keeps smaller copies of every video: a small one
                // to stream while hovering, and (unless a bigger size class
                // is asked for) at most 1080p to download, to save space.
                bool original = options_.resolution != Resolution::Any && options_.resolution != Resolution::P1080;
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
                    if (!original && h <= 1080 && h > bestFull && height > 1080) {
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

namespace {

// Reads the wallpaper tiles from a MotionBgs page, one at a time so a title
// is never paired with the next tile's video. Each is
//   <a title="NAME live wallpaper" href=/SLUG> ... src=/i/c/364x205/media/ID/FILE[.WxH].jpg
QList<OnlineWallpaper> parseMotionPage(const QString &html) {
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
        // The 1080p copy by default (a few MB instead of tens); the 4K one
        // when 4K is asked for and the wallpaper has it (its preview's name
        // carries the original size; older ones are 1080p only).
        w.fullUrl = "https://motionbgs.com/dl/hd/" + w.id + "/";
        w.resolution = "1920x1080";
        w.bigResolution = m.captured(3).mid(1); // e.g. "3840x2160"
        items << w;
    }
    return items;
}

QString motionFilterFile() { return paths::thumbs() + "/../motionbgs-filter.txt"; }

} // namespace

void OnlineLibrary::loadMotionFilter() {
    if (motionFilterState_ != FilterState::Missing) return;
    // Cached for a week.
    QFile cache(motionFilterFile());
    if (cache.open(QIODevice::ReadOnly)) {
        QStringList lines = QString::fromUtf8(cache.readAll()).split('\n', Qt::SkipEmptyParts);
        qint64 saved = lines.isEmpty() ? 0 : lines.first().toLongLong();
        if (QDateTime::currentSecsSinceEpoch() - saved < 7 * 24 * 3600) {
            for (int i = 1; i < lines.size(); ++i) motionFiltered_.insert(lines[i]);
            motionFilterState_ = FilterState::Ready;
            return;
        }
    }
    motionFilterState_ = FilterState::Loading;
    motionFilterBatch_.clear();
    motionFilterEnd_ = false;
    fetchMotionFilterPages(1);
}

// Fetches the tag's pages eight at a time until one comes back empty.
void OnlineLibrary::fetchMotionFilterPages(int first) {
    const int batch = 8;
    motionFilterPending_ = batch;
    for (int page = first; page < first + batch; ++page) {
        QString url = "https://motionbgs.com/tag:girl/" + (page > 1 ? QString::number(page) + "/" : QString());
        QNetworkReply *reply = get(url);
        connect(reply, &QNetworkReply::finished, this, [this, reply, first, batch] {
            reply->deleteLater();
            QList<OnlineWallpaper> items =
                reply->error() == QNetworkReply::NoError ? parseMotionPage(QString::fromUtf8(reply->readAll()))
                                                         : QList<OnlineWallpaper>();
            if (items.isEmpty()) motionFilterEnd_ = true;
            for (const OnlineWallpaper &w : items) motionFilterBatch_.insert(w.id);
            if (--motionFilterPending_ > 0) return;
            if (!motionFilterEnd_ && first < 120) {
                fetchMotionFilterPages(first + batch);
                return;
            }
            motionFiltered_ = motionFilterBatch_;
            motionFilterState_ = FilterState::Ready;
            QFile cache(motionFilterFile());
            if (cache.open(QIODevice::WriteOnly)) {
                cache.write(QByteArray::number(QDateTime::currentSecsSinceEpoch()) + "\n");
                for (const QString &id : motionFiltered_) cache.write(id.toUtf8() + "\n");
            }
            QList<int> waiting = motionWaiting_;
            motionWaiting_.clear();
            for (int generation : waiting)
                if (generation == generation_) searchMotionBgs(generation);
        });
    }
}

void OnlineLibrary::searchMotionBgs(int generation) {
    if (options_.strict && motionFilterState_ != FilterState::Ready) {
        loadMotionFilter(); // instant when the list is cached
        if (motionFilterState_ != FilterState::Ready) {
            // Hold the results back until the filter can be applied.
            motionWaiting_ << generation;
            return;
        }
    }
    int page = page_.value(OnlineSource::MotionBgs, 1);
    QString tag = kGenres[int(options_.genre)].motionTag;
    QString genreWords = kGenres[int(options_.genre)].words;

    // Its search only matches names, so a search also opens the genre
    // pages ("tags") for each word: MotionBgs tags describe what is in the
    // wallpaper, like "eyes", "rain" or "car".
    QStringList urls;
    if (!options_.query.isEmpty()) {
        QUrlQuery params;
        params.addQueryItem("q", (options_.query + " " + genreWords).trimmed());
        if (page > 1) params.addQueryItem("page", QString::number(page));
        QUrl url("https://motionbgs.com/search");
        url.setQuery(params);
        urls << url.toString();
        QStringList words = options_.query.toLower().split(QRegularExpression("[^a-z0-9]+"), Qt::SkipEmptyParts);
        QStringList tags;
        if (words.size() > 1) tags << words.join('-');
        for (const QString &word : words) tags += variants(word);
        tags.removeDuplicates();
        for (const QString &t : tags.mid(0, 6))
            urls << "https://motionbgs.com/tag:" + t + "/" + (page > 1 ? QString::number(page) + "/" : QString());
    } else if (!tag.isEmpty()) {
        urls << "https://motionbgs.com/tag:" + tag + "/" + (page > 1 ? QString::number(page) + "/" : QString());
    } else {
        urls << "https://motionbgs.com/" + (page > 1 ? QString::number(page) + "/" : QString());
    }

    auto remaining = QSharedPointer<int>::create(urls.size());
    auto anyFull = QSharedPointer<bool>::create(false);
    auto gathered = QSharedPointer<QList<OnlineWallpaper>>::create();
    for (const QString &url : urls) {
        QNetworkReply *reply = get(url);
        connect(reply, &QNetworkReply::finished, this, [this, reply, generation, page, remaining, anyFull, gathered] {
            reply->deleteLater();
            // Pages past the end, and tags that do not exist, answer "not
            // found"; that just means nothing more there.
            if (reply->error() == QNetworkReply::NoError) {
                QList<OnlineWallpaper> items = parseMotionPage(QString::fromUtf8(reply->readAll()));
                if (items.size() >= 20) *anyFull = true;
                *gathered += items;
            }
            if (--*remaining > 0) return;
            page_[OnlineSource::MotionBgs] = page + 1;
            if (options_.resolution == Resolution::K4) {
                // Only those with a 4K copy, and that copy is downloaded.
                QList<OnlineWallpaper> big;
                for (OnlineWallpaper w : *gathered) {
                    if (resolutionClass(w.bigResolution) != Resolution::K4) continue;
                    w.fullUrl = "https://motionbgs.com/dl/4k/" + w.id + "/";
                    w.resolution = w.bigResolution;
                    big << w;
                }
                *gathered = big;
            }
            deliver(generation, *gathered);
            sourceDone(generation, OnlineSource::MotionBgs, *anyFull);
        });
    }
}

// --- NASA: videos and pictures --------------------------------------------------

void OnlineLibrary::searchNasa(int generation) {
    int page = page_.value(OnlineSource::Nasa, 1);
    QUrlQuery params;
    QString terms = options_.query.isEmpty() ? QString(options_.genre == Genre::Scenery ? "earth" : "earth from space") : options_.query;
    params.addQueryItem("q", terms);
    params.addQueryItem("media_type", options_.filter == OnlineFilter::Live       ? "video"
                                      : options_.filter == OnlineFilter::Pictures ? "image"
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
    // The size is part of the name: the 1080p and 4K copies of one
    // wallpaper are different files.
    return paths::downloads() + "/" + w.key() + (w.resolution.isEmpty() ? QString() : "-" + w.resolution) + "." + suffix;
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

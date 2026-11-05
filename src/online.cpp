#include "online.h"

#include "paths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSharedPointer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QLocale>
#include <QUrl>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QUrlQuery>

#include <algorithm>
#include <memory>

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
const QStringList kAiWords = {"ai", "aiart", "aiartwork", "aigenerated", "midjourney", "dall", "dalle", "diffusion",
                              "generated", "generative", "aigc", "novelai", "nijijourney", "sdxl", "comfyui",
                              "automatic1111", "dreamshaper", "leonardoai", "dreamstudio", "neuralart"};

// Strict filter: tags that mark a wallpaper as showing (anime) girls, where
// most suggestive ones are. Whole words, in the tags sources give.
const QStringList kStrictTagWords = {"girl", "girls", "waifu", "gravure", "schoolgirl", "catgirl", "foxgirl",
                                     "bunnygirl", "loli", "oppai"};

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

QString OnlineWallpaper::key() const {
    return OnlineLibrary::sourceName(source).toLower().remove(' ') + "-" + id;
}

OnlineLibrary::OnlineLibrary(QObject *parent) : QObject(parent) {}

QString OnlineLibrary::sourceName(OnlineSource source) {
    switch (source) {
    case OnlineSource::Wallhaven: return "Wallhaven";
    case OnlineSource::MotionBgs: return "MotionBgs";
    case OnlineSource::Commons: return "Wikimedia";
    case OnlineSource::Nasa: return "NASA";
    case OnlineSource::XLive: return "X Live Wallpaper";
    case OnlineSource::Sucrose: return "Sucrose";
    case OnlineSource::Bing: return "Bing";
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
    // Held at 1 while starting, so a source that answers straight away
    // (Sucrose with its index in memory) cannot finish the search early.
    pending_ = 1;
    for (OnlineSource source : options_.sources) {
        if (more && !hasMore_.value(source)) continue;
        // Wallhaven only has pictures, MotionBgs only live wallpapers, and
        // Commons and NASA have nothing for some genres.
        if (source == OnlineSource::Wallhaven && options_.filter == OnlineFilter::Live) continue;
        if (source == OnlineSource::MotionBgs && options_.filter == OnlineFilter::Pictures) continue;
        if (source == OnlineSource::Commons && !kGenres[int(options_.genre)].commons) continue;
        if (source == OnlineSource::Nasa && !kGenres[int(options_.genre)].nasa) continue;
        // NASA and X Live do not say how big their files are; MotionBgs only has 1080p and 4K.
        if ((source == OnlineSource::Nasa || source == OnlineSource::XLive) && options_.resolution != Resolution::Any)
            continue;
        // X Live and Sucrose have only live wallpapers, Bing only pictures.
        if ((source == OnlineSource::XLive || source == OnlineSource::Sucrose) && options_.filter == OnlineFilter::Pictures)
            continue;
        if (source == OnlineSource::Bing && options_.filter == OnlineFilter::Live) continue;
        // Sucrose does not say how big its videos are; Bing has 1080p and 4K (UHD) copies.
        if (source == OnlineSource::Sucrose && options_.resolution != Resolution::Any) continue;
        if (source == OnlineSource::Bing && (options_.resolution == Resolution::K2 || options_.resolution == Resolution::K8))
            continue;
        // Bing's daily pictures are photos of places: scenery, or any genre.
        if (source == OnlineSource::Bing && options_.genre != Genre::Any && options_.genre != Genre::Scenery) continue;
        if (source == OnlineSource::MotionBgs &&
            (options_.resolution == Resolution::K2 || options_.resolution == Resolution::K8))
            continue;
        ++pending_;
        switch (source) {
        case OnlineSource::Wallhaven: searchWallhaven(generation); break;
        case OnlineSource::MotionBgs: searchMotionBgs(generation); break;
        case OnlineSource::Commons: searchCommons(generation); break;
        case OnlineSource::Nasa: searchNasa(generation); break;
        case OnlineSource::XLive: searchXLive(generation); break;
        case OnlineSource::Sucrose: searchSucrose(generation); break;
        case OnlineSource::Bing: searchBing(generation); break;
        }
    }
    if (--pending_ == 0) { // every source already answered (or none was asked)
        bool any = false;
        for (bool value : hasMore_) any |= value;
        emit searchFinished(any);
    }
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
            // The tags too, word by word: the girls' tags, and the blocklist
            // (not on Commons' categories, where "Bra" is a town).
            static const QRegularExpression split("[^a-z]+");
            for (const QString &word : lower.split(split, Qt::SkipEmptyParts))
                bad = bad || kStrictTagWords.contains(word) ||
                      (w.source != OnlineSource::Commons && kBlocked.contains(word));
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

// --- X Live Wallpaper: Lively packages -----------------------------------------

namespace {
const char *const kXLiveSite = "https://coolleget.com";
} // namespace

QList<OnlineWallpaper> parseXLivePage(const QString &html, int page, bool &more) {
    QList<OnlineWallpaper> items;
    // One entry: the title link (to its page; the name is in its title,
    // which itself holds <b> tags), a short description, the picture.
    static const QRegularExpression entry(
        R"re(<h2[^>]*>\s*<a href="(/livelywallpapers/([^"/]+)\.html)" title="([^"]*)">.*?</a>\s*</h2>\s*<p>(.*?)</p>.*?data-original="([^"]+)")re",
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tags("<[^>]+>");
    for (auto it = entry.globalMatch(html); it.hasNext();) {
        QRegularExpressionMatch m = it.next();
        OnlineWallpaper w;
        w.source = OnlineSource::XLive;
        w.id = m.captured(2);
        w.title = QString(m.captured(3)).remove(tags).remove(" - Wallpaper").trimmed();
        w.video = true; // videos and GIFs: live wallpapers
        QString picture = m.captured(5);
        // The list shows a small copy ("-lp"); the page's own picture is bigger.
        picture.replace(QRegularExpression("-lp(\\.[a-zA-Z]+)$"), "\\1");
        w.thumbUrl = kXLiveSite + picture;
        w.assetsUrl = kXLiveSite + m.captured(1);
        w.tags = QString(m.captured(4)).remove(tags).toLower().split(QRegularExpression("[^a-z0-9]+"), Qt::SkipEmptyParts);
        items << w;
    }
    more = html.contains(QString("list_6_%1.html").arg(page + 1));
    return items;
}

QString parseXLiveZip(const QString &html) {
    static const QRegularExpression zip(R"re(href="(https?://[^"]+\.zip)")re", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = zip.match(html);
    return m.hasMatch() ? m.captured(1) : QString();
}

void OnlineLibrary::searchXLive(int generation) {
    int page = page_.value(OnlineSource::XLive, 1);
    QString url = QString(kXLiveSite) + "/livelywallpapers/" + (page > 1 ? QString("list_6_%1.html").arg(page) : "");
    QNetworkReply *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, page] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            sourceDone(generation, OnlineSource::XLive, false);
            return;
        }
        bool more = false;
        QList<OnlineWallpaper> items = parseXLivePage(QString::fromUtf8(reply->readAll()), page, more);
        // About 30 wallpapers and no search of its own: words and genres
        // are matched against the names and descriptions here.
        QList<OnlineWallpaper> kept;
        for (const OnlineWallpaper &w : items)
            if (matchesWords(w.title + " " + w.tags.join(' '), true)) kept << w;
        items = kept;
        page_[OnlineSource::XLive] = page + 1;
        deliver(generation, items);
        sourceDone(generation, OnlineSource::XLive, more);
    });
}

bool OnlineLibrary::matchesWords(const QString &text, bool genre) const {
    QString lower = text.toLower();
    QString wanted = options_.query + (genre ? QString(" ") + kGenres[int(options_.genre)].words : QString());
    for (const QString &word : wanted.toLower().split(QRegularExpression("[^a-z0-9]+"), Qt::SkipEmptyParts)) {
        bool any = false;
        for (const QString &v : variants(word)) any = any || lower.contains(v);
        if (!any) return false;
    }
    return true;
}

// --- Sucrose: live wallpapers from its store on GitHub -----------------------

namespace {
const char *const kSucroseBase = "https://raw.githubusercontent.com/Taiizor/Store/develop/";

// "src/Abstract/Neon Wave-1" + "anw.mp4" -> its URL, every part encoded.
QString sucroseUrl(const QString &folder, const QString &file) {
    QStringList parts;
    for (const QString &part : (folder + "/" + file).split('/', Qt::SkipEmptyParts))
        parts << QString::fromUtf8(QUrl::toPercentEncoding(part));
    return kSucroseBase + parts.join('/');
}

// A plain file name: no folders, no "..", no links elsewhere.
bool plainFileName(const QString &name) {
    return !name.isEmpty() && !name.contains('/') && !name.contains('\\') && !name.startsWith('.') &&
           !name.contains(':');
}

QJsonObject jsonObject(QByteArray json) {
    if (json.startsWith("\xEF\xBB\xBF")) json.remove(0, 3); // a byte order mark
    return QJsonDocument::fromJson(json).object();
}
} // namespace

QList<SucroseEntry> parseSucroseStore(const QByteArray &json) {
    // {"Categories": {"Abstract": {"Wallpapers": {"Neon Wave-1": {"Source": "src/Abstract", "Adult": false, ...}}}}}
    QList<QList<SucroseEntry>> byCategory;
    QJsonObject categories = jsonObject(json).value("Categories").toObject();
    for (auto c = categories.begin(); c != categories.end(); ++c) {
        QList<SucroseEntry> list;
        QJsonObject wallpapers = c.value().toObject().value("Wallpapers").toObject();
        for (auto w = wallpapers.begin(); w != wallpapers.end(); ++w) {
            QJsonObject item = w.value().toObject();
            QString source = item.value("Source").toString();
            if (!source.startsWith("src/") || source.contains("..") || w.key().contains('/') || w.key().contains(".."))
                continue;
            SucroseEntry entry;
            entry.category = c.key();
            entry.name = w.key();
            entry.folder = source + "/" + w.key();
            entry.adult = item.value("Adult").toBool();
            list << entry;
        }
        byCategory << list;
    }
    // The categories take turns, so the first page is not all one category.
    QList<SucroseEntry> mixed;
    for (int i = 0;; ++i) {
        bool any = false;
        for (const QList<SucroseEntry> &list : byCategory)
            if (i < list.size()) {
                mixed << list[i];
                any = true;
            }
        if (!any) break;
    }
    return mixed;
}

QStringList sucroseCategories(Genre genre) {
    switch (genre) {
    case Genre::Any: return {};
    case Genre::Scenery:
        return {"Landscape", "Nature", "Sky", "Enchanted Forests", "Seasons", "Underwater and Marine", "Ambience"};
    case Genre::Space: return {"Space", "Galaxy", "Science Fiction"};
    case Genre::Anime: return {"Anime"};
    case Genre::Games: return {"Game", "Pixel Art"};
    case Genre::Cars: return {"Vehicles"};
    case Genre::Animals: return {"Animals", "Cute"};
    case Genre::City: return {"Cities and Places", "Architecture and Buildings"};
    case Genre::Fantasy: return {"Fantasy", "Enchanted Forests", "Medieval"};
    case Genre::Abstract: return {"Abstract", "Geometric Patterns", "Colorful", "Minimalist", "Dynamic"};
    }
    return {};
}

bool applySucroseInfo(OnlineWallpaper &w, const SucroseEntry &entry, const QByteArray &json) {
    // {"Title": "...", "Thumbnail": "thumbnail.jpg", "Preview": "preview.gif", "Author": "...",
    //  "Type": 3, "Source": "anw.mp4", "Tags": [...]}; Type 0 is a GIF, 3 a video.
    QJsonObject info = jsonObject(json);
    int type = info.value("Type").toInt(-1);
    QString file = info.value("Source").toString();
    QString ext = QFileInfo(file).suffix().toLower();
    bool gif = type == 0 && ext == "gif";
    bool video = type == 3 && QStringList{"mp4", "webm", "mkv", "mov", "m4v", "avi"}.contains(ext);
    if (!(gif || video) || !plainFileName(file)) return false;
    w.source = OnlineSource::Sucrose;
    // Readable, plus a short hash so "Neon Wave" and "Neon_Wave" (or names
    // in other scripts) stay apart.
    w.id = QString(entry.folder).remove(0, 4).replace(QRegularExpression("[^A-Za-z0-9]+"), "-") + "-" +
           QString::fromLatin1(QCryptographicHash::hash(entry.folder.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
    w.title = info.value("Title").toString().trimmed();
    if (w.title.isEmpty()) w.title = QString(entry.name).remove(QRegularExpression("-\\d+$"));
    w.video = true;
    QString thumb = info.value("Thumbnail").toString();
    QString preview = info.value("Preview").toString();
    w.thumbUrl = sucroseUrl(entry.folder, plainFileName(thumb) ? thumb : "thumbnail.jpg");
    w.previewUrl = plainFileName(preview) ? sucroseUrl(entry.folder, preview) : sucroseUrl(entry.folder, file);
    w.fullUrl = sucroseUrl(entry.folder, file);
    w.tags = {entry.category};
    for (const QJsonValue &tag : info.value("Tags").toArray()) w.tags << tag.toString();
    w.credit = info.value("Author").toString().trimmed();
    return true;
}

void OnlineLibrary::searchSucrose(int generation) {
    if (!sucroseStore_.isEmpty()) {
        sucrosePage(generation);
        return;
    }
    // The whole index (2,500 wallpapers, ~0.5 MB) once per run; search is done here.
    QNetworkReply *reply = get(QString(kSucroseBase) + "src/Store.json");
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) sucroseStore_ = parseSucroseStore(reply->readAll());
        if (sucroseStore_.isEmpty()) {
            if (generation == generation_) emit failed("Sucrose: " + reply->errorString());
            sourceDone(generation, OnlineSource::Sucrose, false);
            return;
        }
        sucrosePage(generation);
    });
}

void OnlineLibrary::sucrosePage(int generation) {
    if (generation != generation_) return;
    int page = page_.value(OnlineSource::Sucrose, 1);
    if (page == 1) {
        // This search's matches: genre by category, words by name and
        // category, and never what Sucrose marks as adult (Wallhaven too
        // only ever gives safe-for-work ones).
        QStringList categories = sucroseCategories(options_.genre);
        sucroseMatches_.clear();
        for (const SucroseEntry &entry : sucroseStore_) {
            if (entry.adult) continue;
            if (!categories.isEmpty() && !categories.contains(entry.category)) continue;
            if (!matchesWords(entry.name + " " + entry.category)) continue;
            sucroseMatches_ << entry;
        }
    }
    QList<SucroseEntry> batch = sucroseMatches_.mid((page - 1) * kPageSize, kPageSize);
    page_[OnlineSource::Sucrose] = page + 1;
    bool more = page * kPageSize < sucroseMatches_.size();
    if (batch.isEmpty()) {
        sourceDone(generation, OnlineSource::Sucrose, false);
        return;
    }
    // Each one's own SucroseInfo.json: its file, type, author and tags
    // (the tags let the AI and Strict filters see more than the name).
    auto found = std::make_shared<QList<OnlineWallpaper>>();
    found->resize(batch.size());
    auto ok = std::make_shared<QList<bool>>(batch.size(), false);
    auto left = std::make_shared<int>(batch.size());
    for (int i = 0; i < batch.size(); ++i) {
        SucroseEntry entry = batch[i];
        QNetworkReply *reply = get(sucroseUrl(entry.folder, "SucroseInfo.json"));
        connect(reply, &QNetworkReply::finished, this, [this, reply, generation, entry, i, found, ok, left, more] {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError)
                (*ok)[i] = applySucroseInfo((*found)[i], entry, reply->readAll());
            if (--*left > 0) return;
            QList<OnlineWallpaper> items;
            for (int j = 0; j < found->size(); ++j)
                if ((*ok)[j]) items << (*found)[j];
            deliver(generation, items);
            sourceDone(generation, OnlineSource::Sucrose, more);
        });
    }
}

// --- Bing: the daily pictures of the last two weeks --------------------------

QList<OnlineWallpaper> parseBing(const QByteArray &json, bool uhd) {
    // {"images": [{"startdate": "20261105", "urlbase": "/th?id=OHR.Name_EN-US123", "title": "...",
    //              "copyright": "Place (© Photographer/Agency)", "wp": true}, ...]}
    static const QRegularExpression name("OHR\\.([A-Za-z0-9]+)_");
    QList<OnlineWallpaper> items;
    for (const QJsonValue &value : jsonObject(json).value("images").toArray()) {
        QJsonObject image = value.toObject();
        QString base = image.value("urlbase").toString();
        // Bing marks some days as not licensed for wallpapers.
        if (!image.value("wp").toBool(true) || !base.startsWith("/th?id=")) continue;
        OnlineWallpaper w;
        w.source = OnlineSource::Bing;
        QRegularExpressionMatch m = name.match(base);
        w.id = m.hasMatch() ? m.captured(1) : image.value("startdate").toString();
        if (w.id.isEmpty()) continue;
        w.credit = image.value("copyright").toString().trimmed();
        w.title = image.value("title").toString().trimmed();
        if (w.title.isEmpty() || w.title == "Info") w.title = QString(w.credit).remove(QRegularExpression("\\s*\\(©.*$"));
        w.tags = {w.credit};
        QString site = "https://www.bing.com" + base;
        w.thumbUrl = site + "_1920x1080.jpg&w=400&h=225";
        w.previewUrl = site + "_1920x1080.jpg&w=1280&h=720";
        w.fullUrl = site + (uhd ? "_UHD.jpg" : "_1920x1080.jpg");
        w.resolution = uhd ? "3840x2160" : "1920x1080";
        items << w;
    }
    return items;
}

void OnlineLibrary::searchBing(int generation) {
    // One list only: today and the 7 days before, then days 7-14 (Bing
    // keeps no more). Bing's pictures for this system's country and language.
    QString market = QLocale::system().name().replace('_', '-');
    bool uhd = options_.resolution != Resolution::P1080;
    auto items = std::make_shared<QList<OnlineWallpaper>>();
    auto left = std::make_shared<int>(2);
    for (int from : {0, 7}) {
        QNetworkReply *reply =
            get(QString("https://www.bing.com/HPImageArchive.aspx?format=js&idx=%1&n=8&mkt=%2").arg(from).arg(market));
        connect(reply, &QNetworkReply::finished, this, [this, reply, generation, uhd, items, left] {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) *items += parseBing(reply->readAll(), uhd);
            if (--*left > 0) return;
            QSet<QString> ids;
            QList<OnlineWallpaper> kept;
            for (const OnlineWallpaper &w : *items)
                if (!ids.contains(w.id) && matchesWords(w.title + " " + w.credit)) {
                    ids.insert(w.id);
                    kept << w;
                }
            deliver(generation, kept);
            sourceDone(generation, OnlineSource::Bing, false);
        });
    }
}

// What an X Live package's wallpaper can be, best first after size: videos, then a GIF.
static const QStringList kXLiveVideos = {"mp4", "webm", "mkv", "mov", "avi", "m4v"};

// unzip reads entry names as patterns: [ ] * ? and \ are taken literally
// when escaped. A name that was not UTF-8 shows U+FFFD; '?' matches the
// byte(s) it stands for.
static QString unzipPattern(const QString &name) {
    QString out;
    for (QChar c : name) {
        if (c == QChar(0xFFFD)) {
            if (!out.endsWith('*')) out += '*';
            continue;
        }
        if (QStringLiteral("[]*?\\").contains(c)) out += '\\';
        out += c;
    }
    return out;
}

QString unpackLivelyZip(const QString &zip, const QString &destBase, QString &error) {
    // The package holds a picture, LivelyInfo.json and the wallpaper itself.
    QProcess list;
    list.start("unzip", {"-Z", "-l", zip});
    if (!list.waitForFinished(20000) || list.exitCode() != 0) {
        error = "Could not open the wallpaper's package (is unzip installed?)";
        return QString();
    }
    // "-rw----  5.1 fat 117784823 b- 117282542 defN 20-Apr-30 04:24 video.mp4"
    // The name is everything after the 9th field, spaces kept. Plain files
    // only ("-rw..." or, from some zip makers, "?rw..."): no folders, no
    // links, no header or totals lines.
    static const QRegularExpression entry(
        R"re(^[-?]\S*\s+\S+\s+\S+\s+(\d+)\s+\S+\s+\d+\s+\S+\s+\S+\s+\S+ (.+)$)re");
    QString chosen;
    qint64 chosenSize = -1;
    bool chosenVideo = false;
    for (QString line : QString::fromUtf8(list.readAllStandardOutput()).split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        QRegularExpressionMatch m = entry.match(line);
        if (!m.hasMatch()) continue;
        QString name = m.captured(2);
        QString ext = QFileInfo(name).suffix().toLower();
        bool video = kXLiveVideos.contains(ext);
        if (!video && ext != "gif") continue;
        qint64 size = m.captured(1).toLongLong();
        if ((video && !chosenVideo) || (video == chosenVideo && size > chosenSize)) {
            chosen = name;
            chosenSize = size;
            chosenVideo = video;
        }
    }
    if (chosen.isEmpty()) {
        error = "This wallpaper is not a video (perhaps a web page), which roofce cannot show";
        return QString();
    }
    // Streamed straight into the file (unzip -p): no folders, no links.
    QString to = destBase + "." + QFileInfo(chosen).suffix().toLower();
    QProcess unzip;
    unzip.setStandardOutputFile(to + ".part");
    unzip.start("unzip", {"-p", zip, unzipPattern(chosen)});
    bool ok = unzip.waitForFinished(300000) && unzip.exitStatus() == QProcess::NormalExit && unzip.exitCode() == 0 &&
              QFileInfo(to + ".part").size() > 0;
    QFile::remove(to);
    ok = ok && QFile::rename(to + ".part", to);
    QFile::remove(to + ".part");
    if (!ok) {
        error = "Could not unpack the wallpaper";
        return QString();
    }
    return to;
}

void OnlineLibrary::unpackXLive(const QString &key, const QString &zip) {
    // A video of 100+ MB takes a while: unpacked off the window's thread.
    auto result = std::make_shared<std::pair<QString, QString>>(); // file, error
    QString base = paths::downloads() + "/" + key;
    QThread *thread = QThread::create([zip, base, result] { result->first = unpackLivelyZip(zip, base, result->second); });
    connect(thread, &QThread::finished, this, [this, thread, key, zip, result] {
        thread->deleteLater();
        QFile::remove(zip);
        downloading_.remove(key);
        if (result->first.isEmpty()) {
            emit failed(result->second);
            return;
        }
        emit downloaded(key, result->first);
    });
    thread->start();
}

void OnlineLibrary::resolve(const OnlineWallpaper &w) {
    if (!w.previewUrl.isEmpty() && !w.fullUrl.isEmpty()) {
        emit resolved(w.key(), w.previewUrl, w.fullUrl);
        return;
    }
    if (w.assetsUrl.isEmpty()) return;
    if (w.source == OnlineSource::XLive) {
        // Its own page has the .zip link; the picture stands in for a preview.
        QNetworkReply *reply = get(w.assetsUrl);
        QString key = w.key(), thumb = w.thumbUrl;
        connect(reply, &QNetworkReply::finished, this, [this, reply, key, thumb] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                emit failed("Could not open the wallpaper's page: " + reply->errorString());
                return;
            }
            QString zip = parseXLiveZip(QString::fromUtf8(reply->readAll()));
            if (zip.isEmpty()) {
                emit failed("This X Live Wallpaper page has no download");
                return;
            }
            emit resolved(key, thumb, zip);
        });
        return;
    }
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
    // X Live: the video (or GIF) unpacked from its .zip, named after the key.
    if (w.source == OnlineSource::XLive) {
        for (const QString &ext : kXLiveVideos + QStringList{"gif"}) {
            QString file = paths::downloads() + "/" + w.key() + "." + ext;
            if (QFile::exists(file)) return file;
        }
        return paths::downloads() + "/" + w.key() + ".mp4";
    }
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
    // Set twice before the first finished: one download is enough.
    if (downloading_.contains(w.key())) return;
    // Videos can be hundreds of MB: write them out as they arrive instead
    // of holding them in memory. The temporary name means a broken
    // download never looks like a finished one.
    // X Live: the package (.zip) first, its video unpacked after.
    bool xlive = w.source == OnlineSource::XLive;
    QString zip = paths::downloads() + "/" + w.key() + ".zip";
    auto out = QSharedPointer<QFile>::create((xlive ? zip : path) + ".part");
    if (!out->open(QIODevice::WriteOnly)) {
        emit failed("Could not save the wallpaper");
        return;
    }
    downloading_.insert(w.key());
    QNetworkReply *reply = get(w.fullUrl);
    reply->setReadBufferSize(1 << 20);
    QString key = w.key();
    connect(reply, &QNetworkReply::readyRead, this, [reply, out] { out->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, key](qint64 received, qint64 total) { emit downloadProgress(key, received, total); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, path, out, xlive, zip] {
        reply->deleteLater();
        out->write(reply->readAll());
        out->close();
        if (reply->error() != QNetworkReply::NoError || out->size() == 0) {
            out->remove();
            downloading_.remove(key);
            emit failed("Download failed: " + reply->errorString());
            return;
        }
        if (xlive) {
            QFile::remove(zip);
            QFile::rename(zip + ".part", zip);
            unpackXLive(key, zip);
            return;
        }
        QFile::remove(path);
        QFile::rename(path + ".part", path);
        downloading_.remove(key);
        emit downloaded(key, path);
    });
}

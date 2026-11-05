// The free online library, from sources that need no account: Wallhaven
// (pictures), MotionBgs (live wallpapers), Wikimedia Commons (videos and
// pictures), NASA's image and video library (videos and pictures) and
// X Live Wallpaper's library (live wallpapers, as Lively .zip packages),
// Sucrose's store (live wallpapers, from its GitHub repo) and Bing's daily
// pictures of the last two weeks. Only previews and
// the wallpapers the user picks are downloaded; nothing is stored up front.
#pragma once

#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>

enum class OnlineSource { Wallhaven, MotionBgs, Commons, Nasa, XLive, Sucrose, Bing };
enum class OnlineFilter { All, Live, Pictures };
enum class Genre { Any, Scenery, Space, Anime, Games, Cars, Animals, City, Fantasy, Abstract };
const int kGenreCount = 10;
QString genreName(Genre genre);

// Size classes by the long side: 1080p 1920-2559, 2K 2560-3839, 4K
// 3840-7679, 8K 7680 and up.
enum class Resolution { Any, P1080, K2, K4, K8 };
QString resolutionName(Resolution r);
// The class of a "WIDTHxHEIGHT" or of a size; Any when below 1080p or unknown.
Resolution resolutionClass(int width, int height);
Resolution resolutionClass(const QString &resolution);

struct OnlineWallpaper {
    OnlineSource source = OnlineSource::Wallhaven;
    QString id;
    QString title;
    bool video = false;
    QString thumbUrl;   // still image for the tile, uncropped
    QString previewUrl; // what plays when hovering: a small video, or a picture
    QString fullUrl;    // what is downloaded when set as the wallpaper
    QString assetsUrl;  // NASA, X Live: where previewUrl/fullUrl are looked up
    QString resolution;
    qint64 fileSize = 0; // 0 when the source does not say
    QStringList tags;    // what the source says is in it (Commons categories)
    QString bigResolution; // MotionBgs: size of its 4K copy, when it has one
    QString credit;        // who made it or owns it (Sucrose author, Bing photo credit)

    // Unique across sources; used for file names.
    QString key() const;
};

// X Live Wallpaper (coolleget.com/livelywallpapers): the wallpapers on list
// page `page`, and whether there is a next page.
QList<OnlineWallpaper> parseXLivePage(const QString &html, int page, bool &more);
// One wallpaper in Sucrose's store index (src/Store.json).
struct SucroseEntry {
    QString category, name, folder; // folder: "src/<Category>/<Name>"
    bool adult = false;
};
QList<SucroseEntry> parseSucroseStore(const QByteArray &json);
// Sucrose's categories that belong to a genre (empty for Any).
QStringList sucroseCategories(Genre genre);
// Fills in a Sucrose wallpaper from its SucroseInfo.json; false when it is
// not a video or GIF (web pages, YouTube links...).
bool applySucroseInfo(OnlineWallpaper &w, const SucroseEntry &entry, const QByteArray &json);
// Bing's daily pictures (HPImageArchive JSON), without the ones Bing marks
// as not for wallpapers. `uhd` picks the UHD copy, else 1920x1080.
QList<OnlineWallpaper> parseBing(const QByteArray &json, bool uhd);
// The .zip download link on one wallpaper's own page, or "".
QString parseXLiveZip(const QString &html);
// Unpacks a Lively package (.zip): its biggest video, or else a GIF, becomes
// `destBase` + "." + its extension. The file made, or "" with `error` set
// (a web page wallpaper, say).
QString unpackLivelyZip(const QString &zip, const QString &destBase, QString &error);

struct SearchOptions {
    QString query;
    OnlineFilter filter = OnlineFilter::All;
    Genre genre = Genre::Any;
    QList<OnlineSource> sources;
    // Strict: also hides wallpapers sources tag as showing (anime) girls,
    // where most suggestive ones are, on top of the word blocklist.
    bool strict = true;
    bool hideAi = true; // hides AI-generated art where sources say so
    Resolution resolution = Resolution::Any;
};

class OnlineLibrary : public QObject {
    Q_OBJECT
public:
    explicit OnlineLibrary(QObject *parent = nullptr);

    static QString sourceName(OnlineSource source);
    // True when the text asks for something not suitable as a wallpaper.
    static bool blocked(const QString &text);
    static bool aiMade(const QString &text);
    // The word and its singular/plural, so "eyes" also finds "eye".
    static QStringList variants(const QString &word);

    // Searches the chosen sources; `more` asks for their next pages.
    void search(const SearchOptions &options, bool more);
    void fetchThumb(const OnlineWallpaper &w);
    // Fills in previewUrl and fullUrl where they are not known yet (NASA).
    void resolve(const OnlineWallpaper &w);
    void download(const OnlineWallpaper &w);
    // The file a downloaded wallpaper is kept in (it may not exist yet).
    static QString localFile(const OnlineWallpaper &w);

signals:
    void results(const QList<OnlineWallpaper> &items);
    // All sources answered; `more` is true when another page exists.
    void searchFinished(bool more);
    void thumbReady(const QString &key, const QString &path);
    void resolved(const QString &key, const QString &previewUrl, const QString &fullUrl);
    void downloadProgress(const QString &key, qint64 received, qint64 total);
    void downloaded(const QString &key, const QString &path);
    void failed(const QString &message);

private:
    QNetworkReply *get(const QString &url);
    void searchWallhaven(int generation);
    void searchMotionBgs(int generation);
    void searchCommons(int generation);
    void searchNasa(int generation);
    void searchXLive(int generation);
    void searchSucrose(int generation);
    void sucrosePage(int generation);
    void searchBing(int generation);
    // The search words (and with `genre`, the genre's words) all in `text`:
    // for X Live, Sucrose and Bing, which have no search of their own.
    bool matchesWords(const QString &text, bool genre = false) const;
    // X Live: the .zip is downloaded, then its video (or GIF) unpacked.
    void unpackXLive(const QString &key, const QString &zip);
    void sourceDone(int generation, OnlineSource source, bool more);
    void deliver(int generation, QList<OnlineWallpaper> items);
    // MotionBgs' "girl" tag, fetched once a week, for the strict filter.
    void loadMotionFilter();
    void fetchMotionFilterPages(int first);

    QNetworkAccessManager net_;
    SearchOptions options_;
    QSet<QString> seen_; // keys already shown for this search
    QSet<QString> motionFiltered_;
    enum class FilterState { Missing, Loading, Ready } motionFilterState_ = FilterState::Missing;
    QSet<QString> motionFilterBatch_;
    int motionFilterPending_ = 0;
    bool motionFilterEnd_ = false;
    QList<int> motionWaiting_; // searches waiting for the filter
    QMap<OnlineSource, int> page_;     // next page (or offset for Commons)
    QMap<OnlineSource, bool> hasMore_;
    int pending_ = 0;
    int generation_ = 0; // drops answers to an older search
    QSet<QString> downloading_; // keys being downloaded or unpacked
    QList<SucroseEntry> sucroseStore_;   // the index, read once per run
    QList<SucroseEntry> sucroseMatches_; // this search's matches, in pages
};

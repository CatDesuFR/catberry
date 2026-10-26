// The free online library, from sources that need no account: Wallhaven
// (pictures), MotionBgs (live wallpapers), Wikimedia Commons (videos and
// pictures) and NASA's image and video library (videos and pictures). Only previews and
// the wallpapers the user picks are downloaded; nothing is stored up front.
#pragma once

#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>

enum class OnlineSource { Wallhaven, MotionBgs, Commons, Nasa };
enum class OnlineFilter { All, Live, Pictures };
enum class Genre { Any, Scenery, Space, Anime, Games, Cars, Animals, City, Fantasy, Abstract };
const int kGenreCount = 10;
QString genreName(Genre genre);

struct OnlineWallpaper {
    OnlineSource source = OnlineSource::Wallhaven;
    QString id;
    QString title;
    bool video = false;
    QString thumbUrl;   // still image for the tile, uncropped
    QString previewUrl; // what plays when hovering: a small video, or a picture
    QString fullUrl;    // what is downloaded when set as the wallpaper
    QString assetsUrl;  // NASA only: where previewUrl/fullUrl are looked up
    QString resolution;
    qint64 fileSize = 0; // 0 when the source does not say
    QStringList tags;    // what the source says is in it (Commons categories)

    // Unique across sources; used for file names.
    QString key() const;
};

struct SearchOptions {
    QString query;
    OnlineFilter filter = OnlineFilter::All;
    Genre genre = Genre::Any;
    QList<OnlineSource> sources;
    // Strict: also hides wallpapers sources tag as showing (anime) girls,
    // where most suggestive ones are, on top of the word blocklist.
    bool strict = true;
    bool hideAi = true; // hides AI-generated art where sources say so
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
};

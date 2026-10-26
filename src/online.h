// The free online library: Wallhaven (wallhaven.cc), browsed through its
// public API. Only previews and the wallpapers the user picks are
// downloaded; nothing is stored up front.
#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

struct OnlineWallpaper {
    QString id;
    QString fullUrl;
    QString thumbUrl;
    QString resolution;
    qint64 fileSize = 0;
};

class Wallhaven : public QObject {
    Q_OBJECT
public:
    explicit Wallhaven(QObject *parent = nullptr);

    // sorting: "toplist", "date_added", "views", "favorites" or "relevance".
    void search(const QString &query, const QString &sorting, int page);
    void fetchThumb(const OnlineWallpaper &w);
    void download(const OnlineWallpaper &w);
    // The file a downloaded wallpaper is kept in (it may not exist yet).
    static QString localFile(const OnlineWallpaper &w);

signals:
    void results(const QList<OnlineWallpaper> &items, int page, int lastPage);
    void thumbReady(const QString &id, const QString &path);
    void downloadProgress(const QString &id, qint64 received, qint64 total);
    void downloaded(const QString &id, const QString &path);
    void failed(const QString &message);

private:
    QNetworkReply *get(const QString &url);
    QNetworkAccessManager net_;
};

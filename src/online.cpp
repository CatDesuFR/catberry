#include "online.h"

#include "paths.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

Wallhaven::Wallhaven(QObject *parent) : QObject(parent) {}

QNetworkReply *Wallhaven::get(const QString &url) {
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "catberry-roofce/" ROOFCE_VERSION);
    request.setTransferTimeout(30000);
    return net_.get(request);
}

void Wallhaven::search(const QString &query, const QString &sorting, int page) {
    QUrl url("https://wallhaven.cc/api/v1/search");
    QUrlQuery params;
    if (!query.isEmpty()) params.addQueryItem("q", query);
    params.addQueryItem("categories", "111"); // general, anime, people
    params.addQueryItem("purity", "100");     // safe for work only
    params.addQueryItem("sorting", sorting);
    if (sorting == "toplist") params.addQueryItem("topRange", "1y");
    params.addQueryItem("page", QString::number(page));
    url.setQuery(params);

    QNetworkReply *reply = get(url.toString());
    connect(reply, &QNetworkReply::finished, this, [this, reply, page] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed("Could not reach Wallhaven: " + reply->errorString());
            return;
        }
        QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        QList<OnlineWallpaper> items;
        for (const QJsonValue &value : json.value("data").toArray()) {
            QJsonObject o = value.toObject();
            OnlineWallpaper w;
            w.id = o.value("id").toString();
            w.fullUrl = o.value("path").toString();
            w.thumbUrl = o.value("thumbs").toObject().value("small").toString();
            w.resolution = o.value("resolution").toString();
            w.fileSize = o.value("file_size").toVariant().toLongLong();
            if (!w.id.isEmpty() && !w.fullUrl.isEmpty()) items << w;
        }
        int lastPage = json.value("meta").toObject().value("last_page").toInt(page);
        emit results(items, page, lastPage);
    });
}

void Wallhaven::fetchThumb(const OnlineWallpaper &w) {
    QString path = paths::thumbs() + "/wallhaven-" + w.id + ".jpg";
    if (QFile::exists(path)) {
        emit thumbReady(w.id, path);
        return;
    }
    QNetworkReply *reply = get(w.thumbUrl);
    QString id = w.id;
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, path] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QFile out(path);
        if (out.open(QIODevice::WriteOnly) && out.write(reply->readAll()) > 0) {
            out.close();
            emit thumbReady(id, path);
        }
    });
}

QString Wallhaven::localFile(const OnlineWallpaper &w) {
    return paths::downloads() + "/wallhaven-" + w.id + "." + QFileInfo(QUrl(w.fullUrl).path()).suffix();
}

void Wallhaven::download(const OnlineWallpaper &w) {
    QString path = localFile(w);
    if (QFile::exists(path)) {
        emit downloaded(w.id, path);
        return;
    }
    QNetworkReply *reply = get(w.fullUrl);
    QString id = w.id;
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, id](qint64 received, qint64 total) { emit downloadProgress(id, received, total); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, path] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed("Download failed: " + reply->errorString());
            return;
        }
        // Written under a temporary name so a broken download never looks
        // like a finished one.
        QFile out(path + ".part");
        if (!out.open(QIODevice::WriteOnly) || out.write(reply->readAll()) <= 0) {
            emit failed("Could not save the wallpaper");
            return;
        }
        out.close();
        QFile::remove(path);
        QFile::rename(path + ".part", path);
        emit downloaded(id, path);
    });
}

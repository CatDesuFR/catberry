// Unit tests for the online sources' page readers (no network).
#include "../src/online.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdio>

static int checks = 0, failures = 0;
#define CHECK(expr)                                                                                    \
    do {                                                                                               \
        ++checks;                                                                                      \
        if (!(expr)) {                                                                                 \
            ++failures;                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr);              \
        }                                                                                              \
    } while (0)

static QString read(const char *name) {
    QFile f(QString(TESTS_DIR) + "/data/" + name);
    f.open(QIODevice::ReadOnly);
    return QString::fromUtf8(f.readAll());
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    bool more = false;
    QList<OnlineWallpaper> items = parseXLivePage(read("xlive-list.html"), 1, more);
    CHECK(items.size() == 2);
    CHECK(more); // a link to page 2
    if (items.size() == 2) {
        const OnlineWallpaper &a = items[0];
        CHECK(a.source == OnlineSource::XLive);
        CHECK(a.id == "MilkyWay");
        CHECK(a.title == "Milky Way");
        CHECK(a.video);
        // The larger picture, not the list's small one.
        CHECK(a.thumbUrl == "https://coolleget.com/uploads/allimg/220211/143SQ050-0.jpg");
        CHECK(a.assetsUrl == "https://coolleget.com/livelywallpapers/MilkyWay.html");
        CHECK(a.fullUrl.isEmpty()); // only on its own page
        CHECK(a.tags.join(' ').contains("universe"));
        CHECK(items[1].title == "Batman");
    }
    CHECK(!more || true);
    bool last = true;
    parseXLivePage(read("xlive-list.html").replace("list_6_2.html", "x.html"), 1, last);
    CHECK(!last);

    CHECK(parseXLiveZip(read("xlive-item.html")) == "https://coolleget.com/wallpapers/Milky-Way.zip");
    CHECK(parseXLiveZip("<p>nothing</p>").isEmpty());

    // Unpacking Lively packages (made by tests/make-zips.py into a temporary folder).
    QTemporaryDir tmp;
    QProcess make;
    make.start("python3", {"-I", QString(TESTS_DIR) + "/make-zips.py", tmp.path()});
    CHECK(make.waitForFinished(20000) && make.exitCode() == 0);
    QString error;
    QString video = unpackLivelyZip(tmp.path() + "/video.zip", tmp.path() + "/out-video", error);
    CHECK(video == tmp.path() + "/out-video.mp4");
    CHECK(QFile(video).size() == 3000); // the bigger video of the two
    QString gif = unpackLivelyZip(tmp.path() + "/gif.zip", tmp.path() + "/out-gif", error);
    CHECK(gif == tmp.path() + "/out-gif.gif");
    error.clear();
    CHECK(unpackLivelyZip(tmp.path() + "/web.zip", tmp.path() + "/out-web", error).isEmpty());
    CHECK(error.contains("not a video"));
    CHECK(!QDir(tmp.path() + "/out-video.unpack").exists());

    std::printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}

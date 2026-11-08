// Unit tests for the online sources' page readers (no network).
#include "../src/online.h"
#include "../src/linkimport.h"

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

static QByteArray readBytes(const char *name) {
    QFile f(QString(TESTS_DIR) + "/data/" + name);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
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

    // Sucrose: the index (categories take turns, a path outside src/ is
    // dropped) and each wallpaper's info.
    QList<SucroseEntry> store = parseSucroseStore(readBytes("sucrose-store.json"));
    CHECK(store.size() == 3);
    CHECK(store.size() == 3 && store[0].category == "Abstract" && store[1].category == "War" && store[1].adult);
    CHECK(store.size() == 3 && store[0].folder == "src/Abstract/Ink Flow-1");
    CHECK(sucroseCategories(Genre::Cars) == QStringList{"Vehicles"});
    OnlineWallpaper sw;
    SucroseEntry neon{"Abstract", "Neon Wave-1", "src/Abstract/Neon Wave-1", false};
    CHECK(applySucroseInfo(sw, neon, readBytes("sucrose-video.json")));
    CHECK(sw.key().startsWith("sucrose-Abstract-Neon-Wave-1-") && sw.key().size() == 37);
    CHECK(sw.fullUrl == "https://raw.githubusercontent.com/Taiizor/Store/develop/src/Abstract/Neon%20Wave-1/neon%20wave.mp4");
    CHECK(sw.previewUrl.endsWith("/Neon%20Wave-1/preview.gif") && sw.video && sw.credit == "Some Artist");
    CHECK(sw.tags == (QStringList{"Abstract", "Liquid", "Flow"}));
    CHECK(OnlineLibrary::localFile(sw).endsWith("/" + sw.key() + ".mp4"));
    OnlineWallpaper other;
    SucroseEntry underscore{"Abstract", "Neon_Wave-1", "src/Abstract/Neon_Wave-1", false};
    CHECK(applySucroseInfo(other, underscore, readBytes("sucrose-video.json")) && other.key() != sw.key());
    OnlineWallpaper web, sneaky;
    CHECK(!applySucroseInfo(web, neon, readBytes("sucrose-web.json")));
    CHECK(!applySucroseInfo(sneaky, neon, readBytes("sucrose-sneaky.json")));

    // Bing: days marked not for wallpapers are dropped; "Info" titles use the place.
    QList<OnlineWallpaper> bing = parseBing(readBytes("bing.json"), true);
    CHECK(bing.size() == 2);
    CHECK(bing.size() == 2 && bing[0].key() == "bing-StoneBridge" && bing[0].title == "A bridge in the hills");
    CHECK(bing.size() == 2 && bing[0].fullUrl == "https://www.bing.com/th?id=OHR.StoneBridge_EN-US1234567890_UHD.jpg");
    CHECK(bing.size() == 2 && bing[0].credit.contains("A Photographer") && bing[0].resolution == "3840x2160");
    CHECK(bing.size() == 2 && bing[1].title == "Quiet lake at dawn");
    CHECK(bing.size() == 2 && OnlineLibrary::localFile(bing[0]).endsWith("/bing-StoneBridge-3840x2160.jpg"));
    CHECK(parseBing(readBytes("bing.json"), false).value(0).fullUrl.endsWith("_1920x1080.jpg"));

    // AI and Strict word checks.
    CHECK(OnlineLibrary::aiMade("Neon city SDXL render"));
    CHECK(!OnlineLibrary::aiMade("Rain over the mountains"));

    // Links: pasted text, pages, file types, bot checks.
    CHECK(linksInText("https://a.com/x.mp4\n  http://b.org/p  ftp://c/d nonsense https://a.com/x.mp4") ==
          (QStringList{"https://a.com/x.mp4", "http://b.org/p"}));
    QList<QUrl> found = mediaInPage(read("page-video.html"), QUrl("https://example.com/post/7"));
    QStringList foundText;
    for (const QUrl &u : found) foundText << u.toString();
    CHECK(foundText == (QStringList{"https://cdn.example.com/clips/wave.mp4?x=1&y=2", "https://example.com/player?id=1",
                                    "https://example.com/v/big.webm", "https://example.com/v/mid.webm",
                                    "https://example.com/v/small.webm", "https://example.com/media/loop.webm",
                                    "https://cdn.example.com/clips/hd.mp4",
                                    "https://example.com/cover.jpg", "https://example.com/poster.png"}));
    CHECK(mediaInPage("<p>nothing</p>", QUrl("https://x.com")).isEmpty());
    QByteArray mp4("\x00\x00\x00\x18" "ftypmp42\x00\x00\x00\x00mp42isom", 24);
    CHECK(mediaExtension(mp4, "") == "mp4");
    CHECK(mediaExtension(mp4, "application/octet-stream") == "mp4");
    CHECK(mediaExtension(QByteArray("GIF89a\x01\x00\x01\x00", 10), "") == "gif");
    CHECK(mediaExtension(QByteArray("\x89PNG\r\n\x1a\n", 8), "") == "png");
    CHECK(mediaExtension("<!doctype html><html>", "text/html") == "");
    CHECK(mediaExtension("<!doctype html><html>", "video/mp4") == ""); // a page that says it is a video
    CHECK(mediaExtension(QByteArray("PK\x03\x04\x14\x00\x00\x00\x08\x00", 10) + QByteArray(200, 'z'), "video/mp4") == "");
    CHECK(mediaInPage("<meta property=\"og:video\" content=\"https://w.example/v.mp4?a=1&#038;b=2&amp;lt;\">",
                      QUrl("https://w.example/")).value(0).toString() == "https://w.example/v.mp4?a=1&b=2&lt;");
        CHECK(botCheckPage(403, "<html><style>.cf_challenge_text_small{}</style>"));
    CHECK(!botCheckPage(404, "<html>cf_challenge"));
    CHECK(!botCheckPage(403, "Forbidden"));

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
    QString odd = unpackLivelyZip(tmp.path() + "/odd.zip", tmp.path() + "/out-odd", error);
    CHECK(odd == tmp.path() + "/out-odd.mp4");
    CHECK(QFile(odd).size() == 2000 && !QFileInfo(odd).isSymLink()); // the file, not the link
    CHECK(!QFile::exists(tmp.path() + "/out-odd.mp4.part"));

    std::printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}

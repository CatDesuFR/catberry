#include "fonts.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <memory>

namespace {

const char *const kOfl = "Open Font License (free)";

QList<FontEntry> makeCatalog() {
    QList<FontEntry> list;
    // The Mond skin's font. Free for personal use, but no site lets a
    // program download it, so it is fetched by hand and imported.
    list << FontEntry{"Anurati", "", "Free for personal use", true,
                      "https://duckduckgo.com/?q=Anurati+font+free+download"};
    // Fonts popular for desktop clocks and widgets.
    const struct {
        const char *family, *dir;
        bool suggested;
    } fonts[] = {
        {"Montserrat", "montserrat", true},       {"Raleway", "raleway", true},
        {"Bebas Neue", "bebasneue", true},        {"Josefin Sans", "josefinsans", true},
        {"Orbitron", "orbitron", true},           {"Michroma", "michroma", true},
        {"Julius Sans One", "juliussansone", true}, {"Quicksand", "quicksand", true},
        {"Syncopate", "syncopate", true},         {"Poppins", "poppins", true},
        {"Oswald", "oswald", false},              {"Roboto", "roboto", false},
        {"Lato", "lato", false},                  {"Inter", "inter", false},
        {"Nunito", "nunito", false},              {"Comfortaa", "comfortaa", false},
        {"Exo 2", "exo2", false},                 {"Rajdhani", "rajdhani", false},
        {"Righteous", "righteous", false},        {"Audiowide", "audiowide", false},
        {"Teko", "teko", false},                  {"Kanit", "kanit", false},
        {"Questrial", "questrial", false},        {"Space Grotesk", "spacegrotesk", false},
        {"Major Mono Display", "majormonodisplay", false}, {"Monoton", "monoton", false},
        {"Playfair Display", "playfairdisplay", false}, {"Lobster", "lobster", false},
        {"Pacifico", "pacifico", false},          {"Archivo", "archivo", false},
    };
    for (const auto &f : fonts) list << FontEntry{f.family, f.dir, kOfl, f.suggested, QString()};
    // Suggested ones first, keeping Anurati on top.
    std::stable_sort(list.begin(), list.end(),
                     [](const FontEntry &a, const FontEntry &b) { return a.suggested && !b.suggested; });
    return list;
}

QStringList fontFiles() {
    QStringList files;
    QDirIterator it(FontLibrary::folder(), {"*.ttf", "*.otf", "*.ttc", "*.TTF", "*.OTF"}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) files << it.next();
    return files;
}

// Registers a font file once and remembers its id and families (asking Qt
// for a font's families again after another font was removed can come
// back empty).
struct Registered {
    int id;
    QStringList families;
};
QHash<QString, Registered> &fontIds() {
    static QHash<QString, Registered> ids;
    return ids;
}
const Registered *registerFont(const QString &file) {
    auto it = fontIds().find(file);
    if (it == fontIds().end()) {
        int id = QFontDatabase::addApplicationFont(file);
        if (id < 0) return nullptr;
        it = fontIds().insert(file, Registered{id, QFontDatabase::applicationFontFamilies(id)});
    }
    return &it.value();
}

// Lets other programs (and the next start of this one) see font changes.
void refreshFontCache() { QProcess::startDetached("fc-cache", {"-f", FontLibrary::folder()}); }

QString safeName(const QString &family) { return QString(family).toLower().remove(QRegularExpression("[^a-z0-9]")); }

} // namespace

const QList<FontEntry> &fontCatalog() {
    static const QList<FontEntry> catalog = makeCatalog();
    return catalog;
}

FontLibrary::FontLibrary(QObject *parent) : QObject(parent) {}

QString FontLibrary::folder() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/fonts/catberry";
    QDir().mkpath(dir);
    return dir;
}

void FontLibrary::registerAll() {
    for (const QString &file : fontFiles()) registerFont(file);
}

QStringList FontLibrary::savedFamilies() {
    QStringList families;
    for (const QString &file : fontFiles())
        if (const Registered *font = registerFont(file)) families += font->families;
    families.removeDuplicates();
    families.sort(Qt::CaseInsensitive);
    return families;
}

bool FontLibrary::isSaved(const QString &family) {
    return QDir(folder() + "/" + safeName(family)).exists() || savedFamilies().contains(family, Qt::CaseInsensitive);
}

void FontLibrary::finish(const QString &family) {
    registerAll();
    refreshFontCache();
    emit downloaded(family);
}

void FontLibrary::download(const FontEntry &font) {
    if (!font.googleDir.isEmpty()) {
        downloadGoogle(font, {"ofl", "apache", "ufl"});
        return;
    }
    emit failed(font.family, font.family + " has to be downloaded in your browser, then imported on Saved fonts.");
}

// Google keeps each family in ofl/, apache/ or ufl/, with a METADATA.pb
// listing its font files.
void FontLibrary::downloadGoogle(const FontEntry &font, const QStringList &licenseDirs) {
    if (licenseDirs.isEmpty()) {
        emit failed(font.family, "Could not find " + font.family);
        return;
    }
    QString base = "https://raw.githubusercontent.com/google/fonts/main/" + licenseDirs.first() + "/" + font.googleDir + "/";
    QNetworkReply *reply = net_.get(QNetworkRequest(QUrl(base + "METADATA.pb")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, font, licenseDirs, base] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            downloadGoogle(font, licenseDirs.mid(1));
            return;
        }
        static const QRegularExpression fileLine("filename: \"([^\"]+)\"");
        QStringList files;
        auto it = fileLine.globalMatch(QString::fromUtf8(reply->readAll()));
        while (it.hasNext()) {
            QString name = it.next().captured(1);
            // Upright styles are enough for widgets.
            if (!name.contains("Italic")) files << name;
        }
        if (files.isEmpty()) {
            emit failed(font.family, "Could not find " + font.family);
            return;
        }
        QString dir = folder() + "/" + safeName(font.family);
        QDir().mkpath(dir);
        auto remaining = std::make_shared<int>(files.size());
        auto ok = std::make_shared<bool>(true);
        for (const QString &name : files) {
            QNetworkReply *file = net_.get(QNetworkRequest(QUrl(base + QUrl::toPercentEncoding(name))));
            connect(file, &QNetworkReply::finished, this, [this, file, dir, name, font, remaining, ok] {
                file->deleteLater();
                QByteArray data = file->readAll();
                if (file->error() == QNetworkReply::NoError && !data.isEmpty()) {
                    QFile out(dir + "/" + name);
                    if (out.open(QIODevice::WriteOnly)) out.write(data);
                } else {
                    *ok = false;
                }
                if (--*remaining > 0) return;
                if (*ok)
                    finish(font.family);
                else
                    emit failed(font.family, "Part of " + font.family + " could not be downloaded");
            });
        }
    });
}

QStringList FontLibrary::import(const QStringList &files, QString &error) {
    QString dir = folder() + "/imported";
    QDir().mkpath(dir);
    QStringList families;
    // Font sites hand out .zip files: take the fonts out of them first.
    QStringList fontFilesIn;
    for (const QString &file : files) {
        if (QFileInfo(file).suffix().toLower() != "zip") {
            fontFilesIn << file;
            continue;
        }
        QString unpacked = folder() + "/unpacking";
        QDir(unpacked).removeRecursively();
        QDir().mkpath(unpacked);
        QProcess unzip;
        unzip.start("unzip", {"-o", "-j", "-qq", file, "*.otf", "*.ttf", "*.ttc", "*.OTF", "*.TTF", "-d", unpacked});
        if (!unzip.waitForFinished(20000)) error += QFileInfo(file).fileName() + ": could not be unpacked\n";
        QStringList found = QDir(unpacked).entryList({"*.otf", "*.ttf", "*.ttc", "*.OTF", "*.TTF"}, QDir::Files);
        if (found.isEmpty()) error += QFileInfo(file).fileName() + ": no fonts inside\n";
        for (const QString &name : found) fontFilesIn << unpacked + "/" + name;
    }
    for (const QString &file : fontFilesIn) {
        QString ext = QFileInfo(file).suffix().toLower();
        if (ext != "ttf" && ext != "otf" && ext != "ttc") {
            error += QFileInfo(file).fileName() + ": not a font file (.ttf, .otf)\n";
            continue;
        }
        QString target = dir + "/" + QFileInfo(file).fileName();
        QFile::remove(target);
        if (!QFile::copy(file, target)) {
            error += QFileInfo(file).fileName() + ": could not be copied\n";
            continue;
        }
        if (fontIds().contains(target)) QFontDatabase::removeApplicationFont(fontIds().take(target).id);
        const Registered *font = registerFont(target);
        if (!font) {
            QFile::remove(target);
            error += QFileInfo(file).fileName() + ": not a font this computer can read\n";
            continue;
        }
        families += font->families;
    }
    QDir(folder() + "/unpacking").removeRecursively();
    if (!families.isEmpty()) refreshFontCache();
    families.removeDuplicates();
    return families;
}

void FontLibrary::remove(const QString &family) {
    for (const QString &file : fontFiles()) {
        const Registered *font = registerFont(file);
        if (font && font->families.contains(family)) {
            QFontDatabase::removeApplicationFont(font->id);
            fontIds().remove(file);
            QFile::remove(file);
        }
    }
    QDir(folder() + "/" + safeName(family)).removeRecursively();
    refreshFontCache();
}

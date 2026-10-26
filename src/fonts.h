// The font library: popular free fonts to download (Google Fonts, plus
// Anurati, the Mond font) and fonts the user imports. Fonts are installed
// for this user (~/.local/share/fonts/catberry), so other apps can use
// them too.
#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>

struct FontEntry {
    QString family;
    QString googleDir;  // folder name in Google's font collection
    QString license;
    bool suggested = false;
    QString webUrl;     // fonts no program can download: where to get them by hand
};

// Suggested fonts first, Anurati at the very top.
const QList<FontEntry> &fontCatalog();

class FontLibrary : public QObject {
    Q_OBJECT
public:
    explicit FontLibrary(QObject *parent = nullptr);

    static QString folder();
    // Makes every saved font usable by this program.
    static void registerAll();
    // Families of the saved fonts (downloaded and imported).
    static QStringList savedFamilies();
    static bool isSaved(const QString &family);

    void download(const FontEntry &font);
    // Copies .ttf/.otf files (or the fonts inside a .zip) in; returns the
    // families added, or sets `error`.
    QStringList import(const QStringList &files, QString &error);
    void remove(const QString &family);

signals:
    void downloaded(const QString &family);
    void failed(const QString &family, const QString &message);

private:
    void downloadGoogle(const FontEntry &font, const QStringList &licenseDirs);
    void finish(const QString &family);
    QNetworkAccessManager net_;
};

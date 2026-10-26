// The roofce window: Wallpaper (current wallpaper and its settings),
// Library (uploads, Wallpaper Engine, online) and Upload.
#pragma once

#include "library.h"
#include "online.h"
#include "settings.h"

#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTabBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    enum Source { Mine, WallpaperEngine, Online };

    QWidget *buildWallpaperPage();
    QWidget *buildLibraryPage();
    QWidget *buildUploadPage();

    void showPage(int page);
    void refreshCurrent();
    void refreshLibrary();
    void loadThumb(int row, const QString &path, bool video);
    void selectionChanged();
    void applySelected();
    void apply(const QString &file, const QString &title);
    void saveOptions();
    void upload(const QStringList &files);
    void searchOnline(bool more);

    Settings settings_;
    Wallhaven wallhaven_;
    QList<Wallpaper> local_;
    QList<OnlineWallpaper> online_;
    int onlinePage_ = 1, onlineLastPage_ = 1;
    int generation_ = 0; // discards thumbnails meant for an older list

    QListWidget *nav_;
    QStackedWidget *pages_;

    QLabel *currentPreview_, *currentTitle_, *status_;
    QComboBox *fit_;
    QCheckBox *sound_, *autostart_;
    QSlider *volume_;

    QTabBar *sources_;
    QWidget *searchRow_;
    QLineEdit *search_;
    QComboBox *sorting_;
    QListWidget *grid_;
    QLabel *details_, *libraryNote_;
    QPushButton *applyButton_, *deleteButton_, *moreButton_, *steamButton_;

    QLabel *uploadResult_;
};

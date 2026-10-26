// The roofce window: Wallpaper (current wallpaper and its settings),
// Library (uploads, Wallpaper Engine, online) and Upload.
#pragma once

#include "library.h"
#include "online.h"
#include "settings.h"

#include <QHash>
#include <QIcon>
#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMediaPlayer;
class QMovie;
class QVideoWidget;
class QPropertyAnimation;
class QPushButton;
class QToolButton;
class QSlider;
class QStackedWidget;
class QTabBar;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    enum Source { Mine, WallpaperEngine, Online };

    QWidget *buildWallpaperPage();
    QWidget *buildLibraryPage();
    QWidget *buildUploadPage();
    QWidget *buildWidgetsPage();
    void saveWidgets();

    void showPage(int page);
    void refreshCurrent();
    void showPreview();
    void pausePreview();
    void refreshLibrary();
    void loadThumb(const QString &key, const QString &path, bool video);
    // Rebuilds the tiles from local_ or online_, in the chosen order.
    void populateGrid();
    void sortWallpapers();
    QSize localResolution(const Wallpaper &w);
    void selectionChanged();
    void applySelected();
    void apply(const QString &file, const QString &title);
    void saveOptions();
    void upload(const QStringList &files);
    void searchOnline(bool more);
    bool matchesSearch(const Wallpaper &w) const;
    void updateSourcesLabel();
    void hoverItem(QListWidgetItem *item);
    void stopHover();
    void playHover(const QString &url, bool video, bool local);

    Settings settings_;
    OnlineLibrary onlineLib_;
    QList<Wallpaper> local_;
    QList<OnlineWallpaper> online_;
    int generation_ = 0; // discards thumbnails meant for an older list

    QListWidget *nav_;
    QStackedWidget *pages_;

    // The preview plays GIFs and videos too, while the page is visible.
    QStackedWidget *previewStack_;
    QLabel *currentPreview_, *currentTitle_, *status_;
    QVideoWidget *previewVideo_;
    QMediaPlayer *previewPlayer_;
    QMovie *previewMovie_ = nullptr;
    QString previewFile_;
    QComboBox *fit_;
    QCheckBox *sound_, *autostart_;
    QSlider *volume_;

    QTabBar *sources_;
    QWidget *searchRow_;
    QLineEdit *search_;
    QComboBox *filter_, *genre_, *resolution_;
    QToolButton *sourcesButton_;
    QList<QAction *> sourceActions_; // one tick per online source
    QCheckBox *strict_, *hideAi_;
    QWidget *onlineOptions_;
    QListWidget *grid_;
    QLabel *details_, *libraryNote_;
    QComboBox *sort_;
    QHash<QString, QIcon> icons_; // tile pictures by wallpaper key, so re-sorting is instant
    QHash<QString, QSize> resolutions_;
    QPropertyAnimation *scrollAnimation_;
    int scrollTarget_ = 0;
    QPushButton *webButton_;
    QPushButton *applyButton_, *deleteButton_, *moreButton_, *steamButton_, *refreshButton_;

    QLabel *uploadResult_;

    // Plays the motion of the library tile under the mouse.
    QStackedWidget *hover_;
    QLabel *hoverLabel_;
    QVideoWidget *hoverVideo_;
    QMediaPlayer *hoverPlayer_;
    QMovie *hoverMovie_ = nullptr;
    int hoverRow_ = -1;
    QString hoverKey_; // online wallpaper waiting for its preview address
    QTimer *hoverDelay_;      // starts the video once the mouse settles on a tile
    QString hoverVideoUrl_;
    bool hoverVideoLocal_ = false;
};

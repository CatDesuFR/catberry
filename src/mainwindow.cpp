#include "mainwindow.h"

#include "paths.h"
#include "wallpaper.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QMimeData>
#include <QMovie>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QStackedWidget>
#include <QTabBar>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace {

const QSize kThumb(240, 135);

const char *const kStyle = R"(
    QWidget { background: #1e1e2e; color: #cdd6f4; font-size: 10pt; }
    QListWidget#nav { background: #181825; border: none; padding-top: 8px; }
    QListWidget#nav::item { padding: 10px 16px; border-radius: 6px; margin: 2px 8px; }
    QListWidget#nav::item:selected { background: #313244; color: #89b4fa; }
    QListWidget#grid { background: #181825; border: none; }
    QListWidget#grid::item { border-radius: 6px; padding: 4px; }
    QListWidget#grid::item:selected { background: #313244; color: #89b4fa; }
    QPushButton { background: #313244; border: 1px solid #45475a; border-radius: 6px; padding: 6px 14px; }
    QPushButton:hover { background: #45475a; }
    QPushButton:disabled { color: #6c7086; }
    QPushButton#primary { background: #89b4fa; color: #1e1e2e; border: none; font-weight: bold; }
    QLineEdit, QComboBox { background: #313244; border: 1px solid #45475a; border-radius: 6px; padding: 5px 8px; }
    QTabBar::tab { background: transparent; padding: 8px 16px; color: #a6adc8; }
    QTabBar::tab:selected { color: #89b4fa; border-bottom: 2px solid #89b4fa; }
    QLabel#heading { font-size: 16pt; font-weight: bold; }
    QLabel#dim { color: #a6adc8; }
    QLabel#drop { border: 2px dashed #585b70; border-radius: 12px; color: #a6adc8; font-size: 12pt; }
)";

QString humanSize(qint64 bytes) {
    if (bytes >= 1024 * 1024) return QString::number(bytes / 1024.0 / 1024.0, 'f', 1) + " MB";
    return QString::number(bytes / 1024) + " KB";
}

QLabel *heading(const QString &text) {
    auto *label = new QLabel(text);
    label->setObjectName("heading");
    return label;
}

QImage scaledImage(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    if (size.isValid()) {
        size.scale(kThumb, Qt::KeepAspectRatioByExpanding);
        reader.setScaledSize(size);
    }
    QImage image = reader.read();
    if (image.isNull()) return image;
    // Crop to the tile so the grid lines up.
    image = image.scaled(kThumb, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    return image.copy((image.width() - kThumb.width()) / 2, (image.height() - kThumb.height()) / 2, kThumb.width(),
                      kThumb.height());
}

} // namespace

MainWindow::MainWindow() : settings_(Settings::load()) {
    setWindowTitle("Roof Carton Engine");
    setAcceptDrops(true);
    resize(1060, 680);
    qApp->setStyleSheet(kStyle);

    auto *central = new QWidget;
    auto *row = new QHBoxLayout(central);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    nav_ = new QListWidget;
    nav_->setObjectName("nav");
    nav_->setFixedWidth(190);
    nav_->addItems({"Wallpaper", "Library", "Upload wallpaper"});
    row->addWidget(nav_);

    pages_ = new QStackedWidget;
    pages_->addWidget(buildWallpaperPage());
    pages_->addWidget(buildLibraryPage());
    pages_->addWidget(buildUploadPage());
    row->addWidget(pages_, 1);
    setCentralWidget(central);

    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::showPage);

    connect(&wallhaven_, &Wallhaven::results, this, [this](const QList<OnlineWallpaper> &items, int page, int lastPage) {
        onlinePage_ = page;
        onlineLastPage_ = lastPage;
        int start = online_.size();
        online_ += items;
        for (int i = 0; i < items.size(); ++i) {
            auto *item = new QListWidgetItem(items[i].resolution);
            item->setSizeHint(kThumb + QSize(12, 34));
            grid_->addItem(item);
            Q_UNUSED(start);
            wallhaven_.fetchThumb(items[i]);
        }
        moreButton_->setVisible(page < lastPage);
        libraryNote_->setText(online_.isEmpty() ? "Nothing found." : "Free wallpapers from wallhaven.cc");
    });
    connect(&wallhaven_, &Wallhaven::thumbReady, this, [this](const QString &id, const QString &path) {
        if (sources_->currentIndex() != Online) return;
        for (int i = 0; i < online_.size(); ++i)
            if (online_[i].id == id) loadThumb(i, path, false);
    });
    connect(&wallhaven_, &Wallhaven::downloadProgress, this, [this](const QString &, qint64 received, qint64 total) {
        if (total > 0) details_->setText(QString("Downloading... %1 of %2").arg(humanSize(received), humanSize(total)));
    });
    connect(&wallhaven_, &Wallhaven::downloaded, this, [this](const QString &id, const QString &path) {
        applyButton_->setEnabled(true);
        apply(path, "Wallhaven " + id);
    });
    connect(&wallhaven_, &Wallhaven::failed, this, [this](const QString &message) {
        applyButton_->setEnabled(true);
        libraryNote_->setText(message);
    });

    nav_->setCurrentRow(settings_.file.isEmpty() ? 1 : 0);
    refreshCurrent();
}

// --- Wallpaper page ---------------------------------------------------------

QWidget *MainWindow::buildWallpaperPage() {
    auto *page = new QWidget;
    auto *col = new QVBoxLayout(page);
    col->setContentsMargins(28, 24, 28, 24);
    col->addWidget(heading("Wallpaper"));

    previewStack_ = new QStackedWidget;
    previewStack_->setFixedSize(480, 270);
    currentPreview_ = new QLabel;
    currentPreview_->setAlignment(Qt::AlignCenter);
    currentPreview_->setStyleSheet("background: #181825; border-radius: 10px;");
    previewVideo_ = new QVideoWidget;
    previewVideo_->setAspectRatioMode(Qt::KeepAspectRatio);
    // No audio output: the preview is always silent.
    previewPlayer_ = new QMediaPlayer(this);
    previewPlayer_->setVideoOutput(previewVideo_);
    previewPlayer_->setLoops(QMediaPlayer::Infinite);
    previewStack_->addWidget(currentPreview_);
    previewStack_->addWidget(previewVideo_);
    col->addWidget(previewStack_);
    currentTitle_ = new QLabel;
    col->addWidget(currentTitle_);
    status_ = new QLabel;
    status_->setObjectName("dim");
    col->addWidget(status_);
    col->addSpacing(12);

    auto *fitRow = new QHBoxLayout;
    fitRow->addWidget(new QLabel("Fit"));
    fit_ = new QComboBox;
    fit_->addItem("Fill the screen (crop)", "fill");
    fit_->addItem("Fit inside the screen", "fit");
    fit_->addItem("Stretch", "stretch");
    fit_->addItem("Center, original size", "center");
    fit_->setCurrentIndex(qMax(0, fit_->findData(settings_.fit)));
    fitRow->addWidget(fit_);
    fitRow->addStretch();
    col->addLayout(fitRow);

    auto *soundRow = new QHBoxLayout;
    sound_ = new QCheckBox("Play video sound");
    sound_->setChecked(settings_.sound);
    soundRow->addWidget(sound_);
    volume_ = new QSlider(Qt::Horizontal);
    volume_->setRange(0, 100);
    volume_->setValue(settings_.volume);
    volume_->setFixedWidth(180);
    soundRow->addWidget(volume_);
    soundRow->addStretch();
    col->addLayout(soundRow);

    autostart_ = new QCheckBox("Start the wallpaper when I log in");
    autostart_->setChecked(settings_.autostart);
    col->addWidget(autostart_);
    col->addSpacing(12);

    auto *buttons = new QHBoxLayout;
    auto *start = new QPushButton("Show wallpaper");
    start->setObjectName("primary");
    auto *stop = new QPushButton("Stop wallpaper");
    buttons->addWidget(start);
    buttons->addWidget(stop);
    buttons->addStretch();
    col->addLayout(buttons);
    col->addStretch();

    connect(fit_, &QComboBox::currentIndexChanged, this, &MainWindow::saveOptions);
    connect(sound_, &QCheckBox::toggled, this, &MainWindow::saveOptions);
    connect(volume_, &QSlider::sliderReleased, this, &MainWindow::saveOptions);
    connect(autostart_, &QCheckBox::toggled, this, &MainWindow::saveOptions);
    connect(start, &QPushButton::clicked, this, [this] {
        if (settings_.file.isEmpty()) {
            nav_->setCurrentRow(1);
            return;
        }
        apply(settings_.file, settings_.title);
    });
    connect(stop, &QPushButton::clicked, this, [this] {
        stopWallpaper();
        refreshCurrent();
    });
    return page;
}

void MainWindow::refreshCurrent() {
    showPreview();
    if (settings_.file.isEmpty())
        currentTitle_->clear();
    else
        currentTitle_->setText(settings_.title.isEmpty() ? QFileInfo(settings_.file).fileName() : settings_.title);
    status_->setText(wallpaperRunning() ? "The wallpaper is running." : "The wallpaper is not running.");
}

void MainWindow::showPreview() {
    if (previewFile_ == settings_.file && !previewFile_.isEmpty()) {
        // Same wallpaper: just resume what was paused.
        if (previewMovie_) previewMovie_->setPaused(false);
        if (previewStack_->currentWidget() == previewVideo_) previewPlayer_->play();
        return;
    }
    previewFile_ = settings_.file;
    previewPlayer_->stop();
    currentPreview_->clear();
    delete previewMovie_;
    previewMovie_ = nullptr;
    previewStack_->setCurrentWidget(currentPreview_);

    if (settings_.file.isEmpty()) {
        currentPreview_->setText("No wallpaper yet: pick one in the Library");
        return;
    }
    switch (kindOfFile(settings_.file)) {
    case Kind::Video:
        previewStack_->setCurrentWidget(previewVideo_);
        previewPlayer_->setSource(QUrl::fromLocalFile(settings_.file));
        previewPlayer_->play();
        return;
    case Kind::Gif: {
        previewMovie_ = new QMovie(settings_.file, QByteArray(), this);
        QSize size = QImageReader(settings_.file).size();
        if (size.isValid()) previewMovie_->setScaledSize(size.scaled(previewStack_->size(), Qt::KeepAspectRatio));
        currentPreview_->setMovie(previewMovie_);
        previewMovie_->start();
        return;
    }
    default: {
        QImage image = QImageReader(settings_.file).read();
        if (image.isNull())
            currentPreview_->setText(QFileInfo(settings_.file).fileName());
        else
            currentPreview_->setPixmap(QPixmap::fromImage(
                image.scaled(previewStack_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    }
}

void MainWindow::pausePreview() {
    if (previewMovie_) previewMovie_->setPaused(true);
    previewPlayer_->pause();
}

void MainWindow::saveOptions() {
    settings_.fit = fit_->currentData().toString();
    settings_.sound = sound_->isChecked();
    settings_.volume = volume_->value();
    settings_.autostart = autostart_->isChecked();
    settings_.save();
    if (!settings_.autostart) QFile::remove(paths::autostartFile());
    if (wallpaperRunning()) {
        QString error;
        showWallpaper(error);
    }
}

void MainWindow::apply(const QString &file, const QString &title) {
    settings_.file = file;
    settings_.title = title;
    settings_.save();
    if (settings_.autostart) {
        QFile desktop(paths::autostartFile());
        if (desktop.open(QIODevice::WriteOnly)) {
            desktop.write("[Desktop Entry]\nType=Application\nName=Roof Carton Engine wallpaper\n"
                          "Comment=catberry roofce: shows your chosen wallpaper\n"
                          "Exec=\"" + QCoreApplication::applicationFilePath().toUtf8() + "\" --wallpaper\n"
                          "X-KDE-autostart-phase=1\nNoDisplay=true\n");
        }
    }
    QString error;
    if (!showWallpaper(error)) QMessageBox::warning(this, "Roof Carton Engine", error);
    details_->setText("Wallpaper set: " + title);
    QTimer::singleShot(800, this, &MainWindow::refreshCurrent);
}

// --- Library page -----------------------------------------------------------

QWidget *MainWindow::buildLibraryPage() {
    auto *page = new QWidget;
    auto *col = new QVBoxLayout(page);
    col->setContentsMargins(28, 24, 28, 24);
    col->addWidget(heading("Library"));

    sources_ = new QTabBar;
    sources_->addTab("My wallpapers");
    sources_->addTab("Wallpaper Engine");
    sources_->addTab("Online (free)");
    sources_->setExpanding(false);
    col->addWidget(sources_);

    searchRow_ = new QWidget;
    auto *searchLayout = new QHBoxLayout(searchRow_);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search wallpapers (e.g. mountains, anime, space)");
    sorting_ = new QComboBox;
    sorting_->addItem("Top this year", "toplist");
    sorting_->addItem("Newest", "date_added");
    sorting_->addItem("Most viewed", "views");
    sorting_->addItem("Most favorited", "favorites");
    auto *go = new QPushButton("Search");
    searchLayout->addWidget(search_, 1);
    searchLayout->addWidget(sorting_);
    searchLayout->addWidget(go);
    col->addWidget(searchRow_);

    libraryNote_ = new QLabel;
    libraryNote_->setObjectName("dim");
    libraryNote_->setWordWrap(true);
    col->addWidget(libraryNote_);

    grid_ = new QListWidget;
    grid_->setObjectName("grid");
    grid_->setViewMode(QListView::IconMode);
    grid_->setIconSize(kThumb);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setSpacing(6);
    grid_->setUniformItemSizes(true);
    grid_->setWordWrap(true);
    col->addWidget(grid_, 1);

    grid_->setMouseTracking(true);
    grid_->viewport()->installEventFilter(this);
    hover_ = new QStackedWidget(grid_->viewport());
    hover_->setAttribute(Qt::WA_TransparentForMouseEvents); // clicks still select the tile
    hover_->setFixedSize(kThumb);
    hoverLabel_ = new QLabel;
    hoverLabel_->setAlignment(Qt::AlignCenter);
    hoverLabel_->setStyleSheet("background: #181825;");
    hoverVideo_ = new QVideoWidget;
    hoverVideo_->setAspectRatioMode(Qt::KeepAspectRatioByExpanding);
    hoverVideo_->setAttribute(Qt::WA_TransparentForMouseEvents);
    hover_->addWidget(hoverLabel_);
    hover_->addWidget(hoverVideo_);
    hover_->hide();
    hoverPlayer_ = new QMediaPlayer(this); // no audio output: silent
    hoverPlayer_->setVideoOutput(hoverVideo_);
    hoverPlayer_->setLoops(QMediaPlayer::Infinite);
    connect(grid_, &QListWidget::itemEntered, this, &MainWindow::hoverItem);
    connect(grid_->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::stopHover);

    moreButton_ = new QPushButton("Load more");
    col->addWidget(moreButton_, 0, Qt::AlignHCenter);

    auto *bottom = new QHBoxLayout;
    details_ = new QLabel;
    details_->setObjectName("dim");
    bottom->addWidget(details_, 1);
    steamButton_ = new QPushButton("Get Wallpaper Engine on Steam");
    deleteButton_ = new QPushButton("Delete");
    applyButton_ = new QPushButton("Set as wallpaper");
    applyButton_->setObjectName("primary");
    bottom->addWidget(steamButton_);
    bottom->addWidget(deleteButton_);
    bottom->addWidget(applyButton_);
    col->addLayout(bottom);

    connect(sources_, &QTabBar::currentChanged, this, &MainWindow::refreshLibrary);
    connect(go, &QPushButton::clicked, this, [this] { searchOnline(false); });
    connect(search_, &QLineEdit::returnPressed, this, [this] { searchOnline(false); });
    connect(sorting_, &QComboBox::currentIndexChanged, this, [this] { searchOnline(false); });
    connect(moreButton_, &QPushButton::clicked, this, [this] { searchOnline(true); });
    connect(grid_, &QListWidget::currentRowChanged, this, &MainWindow::selectionChanged);
    connect(grid_, &QListWidget::itemDoubleClicked, this, &MainWindow::applySelected);
    connect(applyButton_, &QPushButton::clicked, this, &MainWindow::applySelected);
    connect(steamButton_, &QPushButton::clicked, this, [] {
        // Opens Wallpaper Engine's store page in Steam, or the website.
        if (!QDesktopServices::openUrl(QUrl("steam://store/431960")))
            QDesktopServices::openUrl(QUrl("https://store.steampowered.com/app/431960/Wallpaper_Engine/"));
    });
    connect(deleteButton_, &QPushButton::clicked, this, [this] {
        int row = grid_->currentRow();
        if (sources_->currentIndex() != Mine || row < 0 || row >= local_.size()) return;
        const Wallpaper &w = local_[row];
        if (QMessageBox::question(this, "Delete wallpaper", "Delete \"" + w.title + "\" from your wallpapers?") !=
            QMessageBox::Yes)
            return;
        QFile::remove(w.file);
        refreshLibrary();
    });
    return page;
}

void MainWindow::refreshLibrary() {
    stopHover();
    ++generation_;
    grid_->clear();
    local_.clear();
    online_.clear();
    details_->clear();
    int source = sources_->currentIndex();
    searchRow_->setVisible(source == Online);
    moreButton_->setVisible(false);
    deleteButton_->setVisible(source == Mine);
    steamButton_->setVisible(false);

    if (source == Online) {
        libraryNote_->setText("Loading...");
        searchOnline(false);
        return;
    }
    if (source == Mine) {
        local_ = scanUploads();
        libraryNote_->setText(local_.isEmpty() ? "No wallpapers yet. Add some on the Upload wallpaper page."
                                               : "Stored in " + paths::uploads());
    } else {
        local_ = scanWallpaperEngine();
        bool owned = wallpaperEngineInstalled();
        steamButton_->setVisible(!owned);
        if (!owned)
            libraryNote_->setText("Wallpaper Engine is not installed in Steam. Once you own it, subscribe to "
                                  "wallpapers in its Steam Workshop and they appear here, played straight from "
                                  "Steam's folder (no extra copy). Video wallpapers are supported; scene and web "
                                  "wallpapers are not yet.");
        else if (local_.isEmpty())
            libraryNote_->setText("No Workshop wallpapers yet. Subscribe to some in Steam's Wallpaper Engine "
                                  "Workshop; Steam downloads them and they show up here.");
        else
            libraryNote_->setText("Your Wallpaper Engine Workshop subscriptions, read from Steam's folder.");
    }
    for (int i = 0; i < local_.size(); ++i) {
        const Wallpaper &w = local_[i];
        auto *item = new QListWidgetItem(w.title);
        item->setSizeHint(kThumb + QSize(12, 34));
        if (w.kind == Kind::Unsupported) item->setForeground(QColor("#6c7086"));
        grid_->addItem(item);
        if (!w.preview.isEmpty())
            loadThumb(i, w.preview, false);
        else if (w.kind == Kind::Video)
            loadThumb(i, w.file, true);
    }
}

void MainWindow::hoverItem(QListWidgetItem *item) {
    int row = grid_->row(item);
    if (row == hoverRow_) return;
    stopHover();
    if (sources_->currentIndex() == Online || row < 0 || row >= local_.size()) return;
    const Wallpaper &w = local_[row];
    QString gif = w.kind == Kind::Gif ? w.file : w.preview.endsWith(".gif", Qt::CaseInsensitive) ? w.preview : QString();
    if (w.kind != Kind::Video && gif.isEmpty()) return; // a still picture: nothing to play

    hoverRow_ = row;
    QRect tile = grid_->visualItemRect(item);
    // The icon sits centred at the top of the tile.
    hover_->move(tile.x() + (tile.width() - kThumb.width()) / 2, tile.y() + 4);
    if (w.kind == Kind::Video) {
        hover_->setCurrentWidget(hoverVideo_);
        hoverPlayer_->setSource(QUrl::fromLocalFile(w.file));
        hoverPlayer_->play();
    } else {
        hover_->setCurrentWidget(hoverLabel_);
        hoverMovie_ = new QMovie(gif, QByteArray(), this);
        QSize size = QImageReader(gif).size();
        if (size.isValid()) hoverMovie_->setScaledSize(size.scaled(kThumb, Qt::KeepAspectRatioByExpanding));
        hoverLabel_->setMovie(hoverMovie_);
        hoverMovie_->start();
    }
    hover_->show();
    hover_->raise();
}

void MainWindow::stopHover() {
    hoverRow_ = -1;
    hover_->hide();
    hoverPlayer_->stop();
    hoverPlayer_->setSource(QUrl());
    hoverLabel_->clear();
    delete hoverMovie_;
    hoverMovie_ = nullptr;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == grid_->viewport()) {
        if (event->type() == QEvent::Leave) stopHover();
        // Moving onto empty space between tiles.
        if (event->type() == QEvent::MouseMove && !grid_->itemAt(static_cast<QMouseEvent *>(event)->pos()))
            stopHover();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::loadThumb(int row, const QString &path, bool video) {
    int generation = generation_;
    QThreadPool::globalInstance()->start([this, row, path, video, generation] {
        QString source = video ? videoThumbnail(path) : path;
        QImage image = source.isEmpty() ? QImage() : scaledImage(source);
        if (image.isNull()) return;
        QMetaObject::invokeMethod(this, [this, row, image, generation] {
            if (generation != generation_ || row >= grid_->count()) return;
            grid_->item(row)->setIcon(QIcon(QPixmap::fromImage(image)));
        });
    });
}

void MainWindow::selectionChanged() {
    int row = grid_->currentRow();
    applyButton_->setEnabled(row >= 0);
    if (row < 0) return;
    if (sources_->currentIndex() == Online) {
        if (row >= online_.size()) return;
        const OnlineWallpaper &w = online_[row];
        bool have = QFile::exists(Wallhaven::localFile(w));
        details_->setText(QString("%1 - %2%3").arg(w.resolution, humanSize(w.fileSize),
                                                   have ? " - already downloaded" : " - downloaded when you set it"));
        return;
    }
    if (row >= local_.size()) return;
    const Wallpaper &w = local_[row];
    if (w.kind == Kind::Unsupported) {
        details_->setText(w.note);
        applyButton_->setEnabled(false);
    } else {
        details_->setText(kindName(w.kind) + " - " + humanSize(QFileInfo(w.file).size()));
    }
}

void MainWindow::applySelected() {
    int row = grid_->currentRow();
    if (row < 0) return;
    if (sources_->currentIndex() == Online) {
        if (row >= online_.size()) return;
        applyButton_->setEnabled(false);
        wallhaven_.download(online_[row]);
        return;
    }
    if (row >= local_.size() || local_[row].kind == Kind::Unsupported) return;
    apply(local_[row].file, local_[row].title);
}

void MainWindow::searchOnline(bool more) {
    if (sources_->currentIndex() != Online) return;
    if (!more) {
        ++generation_;
        grid_->clear();
        online_.clear();
    }
    wallhaven_.search(search_->text().trimmed(), sorting_->currentData().toString(), more ? onlinePage_ + 1 : 1);
}

// --- Upload page --------------------------------------------------------------

QWidget *MainWindow::buildUploadPage() {
    auto *page = new QWidget;
    auto *col = new QVBoxLayout(page);
    col->setContentsMargins(28, 24, 28, 24);
    col->addWidget(heading("Upload wallpaper"));
    auto *intro = new QLabel("Add your own images (PNG, JPG, WebP, BMP), animated GIFs or videos (MP4, WebM, MKV, "
                             "MOV). They are copied into your library.");
    intro->setObjectName("dim");
    intro->setWordWrap(true);
    col->addWidget(intro);

    auto *drop = new QLabel("Drop files here");
    drop->setObjectName("drop");
    drop->setAlignment(Qt::AlignCenter);
    drop->setMinimumHeight(220);
    col->addWidget(drop);

    auto *choose = new QPushButton("Choose files...");
    choose->setObjectName("primary");
    col->addWidget(choose, 0, Qt::AlignLeft);
    uploadResult_ = new QLabel;
    uploadResult_->setWordWrap(true);
    col->addWidget(uploadResult_);
    col->addStretch();

    connect(choose, &QPushButton::clicked, this, [this] {
        QStringList files = QFileDialog::getOpenFileNames(
            this, "Choose wallpapers", QDir::homePath(),
            "Wallpapers (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.mp4 *.webm *.mkv *.mov *.avi *.m4v)");
        if (!files.isEmpty()) upload(files);
    });
    return page;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    QStringList files;
    for (const QUrl &url : event->mimeData()->urls())
        if (url.isLocalFile()) files << url.toLocalFile();
    if (!files.isEmpty()) {
        nav_->setCurrentRow(2);
        upload(files);
    }
}

void MainWindow::upload(const QStringList &files) {
    QStringList added, errors;
    for (const QString &file : files) {
        QString error;
        QString copy = importFile(file, error);
        if (copy.isEmpty())
            errors << error;
        else
            added << QFileInfo(copy).fileName();
    }
    QString text;
    if (!added.isEmpty()) text += "Added: " + added.join(", ") + ". Find them under Library > My wallpapers.";
    if (!errors.isEmpty()) text += (text.isEmpty() ? "" : "\n") + errors.join("\n");
    uploadResult_->setText(text);
}

// --- navigation -------------------------------------------------------------

void MainWindow::showPage(int page) {
    pages_->setCurrentIndex(page);
    // Only play the preview while it can be seen.
    if (page == 0)
        refreshCurrent();
    else
        pausePreview();
    if (page != 1) stopHover();
    if (page == 1) refreshLibrary();
}

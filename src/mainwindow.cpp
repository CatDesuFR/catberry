#include "mainwindow.h"

#include "paths.h"
#include "wallpaper.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
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
#include <QProcess>
#include <QPropertyAnimation>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSlider>
#include <QStackedWidget>
#include <QTabBar>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QMenu>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoSink>
#include <QVideoWidget>
#include <QWidgetAction>
#include <QWheelEvent>

#include <algorithm>
#include <memory>

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

qint64 pixels(const QString &resolution) {
    QStringList parts = resolution.split('x');
    return parts.size() == 2 ? parts[0].toLongLong() * parts[1].toLongLong() : 0;
}

QString tileKey(const Wallpaper &w) { return "local:" + (w.file.isEmpty() ? w.id : w.file); }

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
    // Opened after an update: the wallpaper may still be the old program.
    restartOutdatedWallpaper();
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
    nav_->addItems({"Wallpaper", "Library", "Upload wallpaper", "Widgets", "Font library", "Saved fonts"});
    FontLibrary::registerAll();
    row->addWidget(nav_);

    pages_ = new QStackedWidget;
    pages_->addWidget(buildWallpaperPage());
    pages_->addWidget(buildLibraryPage());
    pages_->addWidget(buildUploadPage());
    pages_->addWidget(buildWidgetsPage());
    pages_->addWidget(buildFontLibraryPage());
    pages_->addWidget(buildSavedFontsPage());
    connect(&fontLib_, &FontLibrary::downloaded, this, [this](const QString &family) {
        fontNote_->setText(family + " is saved. Find it under Saved fonts.");
        fontDownload_->setEnabled(true);
        refreshFonts();
    });
    connect(&fontLib_, &FontLibrary::failed, this, [this](const QString &, const QString &message) {
        fontNote_->setText(message);
        fontDownload_->setEnabled(true);
    });
    refreshFonts();
    row->addWidget(pages_, 1);
    setCentralWidget(central);

    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::showPage);

    arrivingTimer_ = new QTimer(this);
    arrivingTimer_->setSingleShot(true);
    arrivingTimer_->setInterval(1500);
    connect(arrivingTimer_, &QTimer::timeout, this, &MainWindow::showArriving);
    connect(&onlineLib_, &OnlineLibrary::results, this, [this](const QList<OnlineWallpaper> &items) {
        // Gathered a moment so the sources can take turns, then added at
        // the end: tiles already shown never move.
        arriving_ += items;
        if (!arrivingTimer_->isActive()) arrivingTimer_->start();
        for (const OnlineWallpaper &w : items)
            if (!icons_.contains(w.key())) onlineLib_.fetchThumb(w);
    });
    connect(&onlineLib_, &OnlineLibrary::searchFinished, this, [this](bool more) {
        showArriving();
        moreButton_->setVisible(more);
        QStringList names;
        for (QCheckBox *box : sourceBoxes_)
            if (box->isChecked()) names << box->text();
        QString list = names.size() > 1 ? names.mid(0, names.size() - 1).join(", ") + " and " + names.last()
                                        : names.join("");
        libraryNote_->setText(online_.isEmpty() ? "Nothing found."
                                                : "Free wallpapers from " + list +
                                                      ". Hover a tile to play it; nothing is saved until you set it.");
    });
    connect(&onlineLib_, &OnlineLibrary::thumbReady, this, [this](const QString &key, const QString &path) {
        loadThumb(key, path, false);
    });
    connect(&onlineLib_, &OnlineLibrary::resolved, this,
            [this](const QString &key, const QString &previewUrl, const QString &fullUrl) {
                for (OnlineWallpaper &w : online_) {
                    if (w.key() != key) continue;
                    w.previewUrl = previewUrl;
                    w.fullUrl = fullUrl;
                    if (key == hoverKey_ && w.source != OnlineSource::XLive) playHover(previewUrl, w.video, false);
                }
            });
    connect(&onlineLib_, &OnlineLibrary::downloadProgress, this, [this](const QString &, qint64 received, qint64 total) {
        details_->setText(total > 0 ? QString("Downloading... %1 of %2").arg(humanSize(received), humanSize(total))
                                    : "Downloading... " + humanSize(received));
    });
    connect(&onlineLib_, &OnlineLibrary::downloaded, this, [this](const QString &key, const QString &path) {
        applyButton_->setEnabled(true);
        QString title = key;
        for (const OnlineWallpaper &w : online_)
            if (w.key() == key) title = w.title;
        apply(path, title);
    });
    connect(&onlineLib_, &OnlineLibrary::failed, this, [this](const QString &message) {
        applyButton_->setEnabled(true);
        libraryNote_->setText(message);
    });

    nav_->setCurrentRow(settings_.file.isEmpty() ? 1 : 0);
    refreshCurrent();
}

MainWindow::~MainWindow() {
    // Older versions had an arrange mode; never leave it on.
    if (settings_.arrange) setArrange(false);
}

void MainWindow::setArrange(bool on) {
    settings_.arrange = on;
    if (arrangeButton_) {
        QSignalBlocker block(arrangeButton_);
        arrangeButton_->setChecked(on);
        arrangeButton_->setText(on ? "Done arranging" : "Arrange widgets (drag them on the desktop)");
    }
    saveWidgets();
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
    refreshButton_ = new QPushButton("Refresh");
    sort_ = new QComboBox;
    sort_->addItem("Sort: default");
    sort_->addItem("Sort: resolution, highest first");
    sort_->addItem("Sort: file size, largest first");
    auto *tabsRow = new QHBoxLayout;
    tabsRow->addWidget(sources_, 1);
    tabsRow->addWidget(sort_);
    tabsRow->addWidget(refreshButton_);
    col->addLayout(tabsRow);

    searchRow_ = new QWidget;
    auto *searchLayout = new QHBoxLayout(searchRow_);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search wallpapers");
    filter_ = new QComboBox;
    filter_->addItem("Live and pictures", int(OnlineFilter::All));
    filter_->addItem("Live (videos)", int(OnlineFilter::Live));
    filter_->addItem("Pictures", int(OnlineFilter::Pictures));
    genre_ = new QComboBox;
    for (int g = 0; g < kGenreCount; ++g) genre_->addItem(genreName(Genre(g)), g);
    // Any mix of sources: checkboxes in a menu that stays open while they
    // are ticked; the search runs again once it closes.
    sourcesButton_ = new QToolButton;
    sourcesButton_->setPopupMode(QToolButton::InstantPopup);
    auto *sourcesMenu = new QMenu(sourcesButton_);
    auto ticksBefore = std::make_shared<QList<bool>>();
    for (OnlineSource source : {OnlineSource::MotionBgs, OnlineSource::Sucrose, OnlineSource::XLive, OnlineSource::Wallhaven,
                                OnlineSource::Commons, OnlineSource::Nasa, OnlineSource::Bing}) {
        auto *box = new QCheckBox(source == OnlineSource::Commons ? "Wikimedia Commons" : OnlineLibrary::sourceName(source));
        box->setChecked(true);
        box->setProperty("source", int(source));
        auto *holder = new QWidget;
        auto *holderLayout = new QHBoxLayout(holder);
        holderLayout->setContentsMargins(10, 4, 14, 4);
        holderLayout->addWidget(box);
        auto *action = new QWidgetAction(sourcesMenu);
        action->setDefaultWidget(holder);
        sourcesMenu->addAction(action);
        sourceBoxes_ << box;
        connect(box, &QCheckBox::toggled, this, &MainWindow::updateSourcesLabel);
    }
    auto ticks = [this] {
        QList<bool> on;
        for (QCheckBox *box : sourceBoxes_) on << box->isChecked();
        return on;
    };
    connect(sourcesMenu, &QMenu::aboutToShow, this, [ticksBefore, ticks] { *ticksBefore = ticks(); });
    connect(sourcesMenu, &QMenu::aboutToHide, this, [this, ticksBefore, ticks] {
        if (ticks() != *ticksBefore) searchOnline(false);
    });
    sourcesButton_->setMenu(sourcesMenu);
    updateSourcesLabel();
    strict_ = new QCheckBox("Strict filter");
    strict_->setChecked(true);
    strict_->setToolTip("Also hides wallpapers the sources tag as showing girls (most suggestive ones are there),\n"
                        "on top of the word filter that always applies.");
    hideAi_ = new QCheckBox("Hide AI art");
    hideAi_->setChecked(true);
    auto *go = new QPushButton("Search");
    onlineOptions_ = new QWidget;
    auto *optionsLayout = new QHBoxLayout(onlineOptions_);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->addWidget(genre_);
    optionsLayout->addWidget(sourcesButton_);
    optionsLayout->addWidget(strict_);
    optionsLayout->addWidget(hideAi_);
    searchLayout->addWidget(search_, 1);
    searchLayout->addWidget(filter_);
    resolution_ = new QComboBox;
    for (Resolution r : {Resolution::Any, Resolution::P1080, Resolution::K2, Resolution::K4, Resolution::K8})
        resolution_->addItem(resolutionName(r), int(r));
    resolution_->setToolTip("1080p: 1920-2559 px wide, 2K: 2560-3839, 4K: 3840-7679, 8K: 7680 and up\n"
                            "(the long side, so tall wallpapers count too)");
    searchLayout->addWidget(resolution_);
    searchLayout->addWidget(onlineOptions_);
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
    // Smooth scrolling: by pixels, and the mouse wheel glides (see eventFilter).
    grid_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    grid_->verticalScrollBar()->setSingleStep(24);
    scrollAnimation_ = new QPropertyAnimation(grid_->verticalScrollBar(), "value", this);
    scrollAnimation_->setDuration(220);
    scrollAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    col->addWidget(grid_, 1);

    grid_->setMouseTracking(true);
    grid_->viewport()->setMouseTracking(true);
    grid_->viewport()->installEventFilter(this);
    hover_ = new QStackedWidget(grid_->viewport());
    hover_->setAttribute(Qt::WA_TransparentForMouseEvents); // clicks still select the tile
    hover_->setFixedSize(kThumb);
    hoverLabel_ = new QLabel;
    hoverLabel_->setAlignment(Qt::AlignCenter);
    hoverLabel_->setStyleSheet("background: #181825;");
    hoverVideo_ = new QVideoWidget;
    // The whole wallpaper, uncropped.
    hoverVideo_->setAspectRatioMode(Qt::KeepAspectRatio);
    hoverVideo_->setAttribute(Qt::WA_TransparentForMouseEvents);
    hover_->addWidget(hoverLabel_);
    hover_->addWidget(hoverVideo_);
    hover_->hide();
    hoverPlayer_ = new QMediaPlayer(this); // no audio output: silent
    hoverPlayer_->setVideoOutput(hoverVideo_);
    hoverPlayer_->setLoops(QMediaPlayer::Infinite);
    // The still picture shows at once; the video takes over from its first frame.
    connect(hoverVideo_->videoSink(), &QVideoSink::videoFrameChanged, this, [this] {
        if (hover_->isVisible() && hoverPlayer_->isPlaying()) hover_->setCurrentWidget(hoverVideo_);
    });
    hoverDelay_ = new QTimer(this);
    hoverDelay_->setSingleShot(true);
    hoverDelay_->setInterval(120);
    connect(hoverDelay_, &QTimer::timeout, this, [this] {
        if (hoverVideoUrl_.isEmpty()) return;
        hoverPlayer_->setSource(hoverVideoLocal_ ? QUrl::fromLocalFile(hoverVideoUrl_) : QUrl(hoverVideoUrl_));
        hoverPlayer_->play();
    });
    connect(grid_, &QListWidget::itemEntered, this, &MainWindow::hoverItem);
    connect(grid_->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::stopHover);

    moreButton_ = new QPushButton("Load more");
    col->addWidget(moreButton_, 0, Qt::AlignHCenter);

    auto *bottom = new QHBoxLayout;
    details_ = new QLabel;
    details_->setObjectName("dim");
    bottom->addWidget(details_, 1);
    steamButton_ = new QPushButton("Get Wallpaper Engine on Steam");
    // wallpapers.com does not allow programs to use it, so it opens in the
    // browser; downloaded files can then be added on the Upload page.
    webButton_ = new QPushButton("Browse wallpapers.com");
    webButton_->setToolTip("Opens wallpapers.com in your browser. Add what you download there on the Upload page.");
    bottom->addWidget(webButton_);
    connect(webButton_, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl("https://wallpapers.com/")); });
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
    connect(filter_, &QComboBox::currentIndexChanged, this, [this] { searchOnline(false); });
    connect(genre_, &QComboBox::currentIndexChanged, this, [this] { searchOnline(false); });
    connect(resolution_, &QComboBox::currentIndexChanged, this, [this] { searchOnline(false); });
    connect(strict_, &QCheckBox::toggled, this, [this] { searchOnline(false); });
    connect(hideAi_, &QCheckBox::toggled, this, [this] { searchOnline(false); });
    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::refreshLibrary);
    connect(sort_, &QComboBox::currentIndexChanged, this, &MainWindow::populateGrid);
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
        arriving_.clear();
    details_->clear();
    int source = sources_->currentIndex();
    searchRow_->setVisible(true);
    onlineOptions_->setVisible(source == Online);
    webButton_->setVisible(source == Online);
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
    // Search and the live/pictures choice apply to your own wallpapers too.
    OnlineFilter kindFilter = OnlineFilter(filter_->currentData().toInt());
    local_.erase(std::remove_if(local_.begin(), local_.end(),
                                [&](const Wallpaper &w) {
                                    bool live = w.kind == Kind::Video || w.kind == Kind::Gif;
                                    if (kindFilter == OnlineFilter::Live && !live) return true;
                                    if (kindFilter == OnlineFilter::Pictures && live) return true;
                                    Resolution wanted = Resolution(resolution_->currentData().toInt());
                                    if (wanted != Resolution::Any) {
                                        QSize size = localResolution(w);
                                        if (resolutionClass(size.width(), size.height()) != wanted) return true;
                                    }
                                    return !matchesSearch(w);
                                }),
                 local_.end());
    populateGrid();
    for (const Wallpaper &w : local_) {
        if (icons_.contains(tileKey(w))) continue;
        if (!w.preview.isEmpty())
            loadThumb(tileKey(w), w.preview, false);
        else if (w.kind == Kind::Video)
            loadThumb(tileKey(w), w.file, true);
    }
}

QSize MainWindow::localResolution(const Wallpaper &w) {
    if (resolutions_.contains(w.file)) return resolutions_.value(w.file);
    QSize size;
    if (w.kind == Kind::Video) {
        // Asked from ffprobe once and remembered.
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
                                "-of", "csv=s=x:p=0", w.file});
        if (probe.waitForFinished(5000)) {
            QStringList parts = QString::fromUtf8(probe.readAllStandardOutput()).trimmed().split('x');
            if (parts.size() == 2) size = QSize(parts[0].toInt(), parts[1].toInt());
        }
    } else if (!w.file.isEmpty()) {
        size = QImageReader(w.file).size();
    }
    resolutions_.insert(w.file, size);
    return size;
}

void MainWindow::showArriving() {
    arrivingTimer_->stop();
    if (arriving_.isEmpty()) return;
    // The sources take turns (1st of each, then 2nd of each...), so a small
    // source is not buried under the big ones.
    QHash<int, int> count;
    QHash<QString, int> turn;
    for (const OnlineWallpaper &w : arriving_) turn.insert(w.key(), count[int(w.source)]++);
    std::stable_sort(arriving_.begin(), arriving_.end(), [&turn](const OnlineWallpaper &a, const OnlineWallpaper &b) {
        return turn.value(a.key()) < turn.value(b.key());
    });
    online_ += arriving_;
    arriving_.clear();
    populateGrid();
}

void MainWindow::sortWallpapers() {
    int mode = sort_->currentIndex();
    if (mode == 0) return;
    if (sources_->currentIndex() == Online) {
        std::stable_sort(online_.begin(), online_.end(), [mode](const OnlineWallpaper &a, const OnlineWallpaper &b) {
            // Unknown values (0) sort last.
            return mode == 1 ? pixels(a.resolution) > pixels(b.resolution) : a.fileSize > b.fileSize;
        });
        return;
    }
    QHash<QString, qint64> value;
    for (const Wallpaper &w : local_) {
        QSize res = mode == 1 ? localResolution(w) : QSize();
        value.insert(w.file, mode == 1 ? qint64(res.width()) * res.height() : QFileInfo(w.file).size());
    }
    std::stable_sort(local_.begin(), local_.end(),
                     [&value](const Wallpaper &a, const Wallpaper &b) { return value.value(a.file) > value.value(b.file); });
}

void MainWindow::populateGrid() {
    stopHover();
    QString selected = grid_->currentItem() ? grid_->currentItem()->data(Qt::UserRole).toString() : QString();
    int scroll = grid_->verticalScrollBar()->value();
    sortWallpapers();
    grid_->setUpdatesEnabled(false);
    grid_->clear();
    auto add = [&](const QString &key, const QString &label, const QString &tip, bool dim) {
        auto *item = new QListWidgetItem(label.left(60));
        item->setData(Qt::UserRole, key);
        item->setToolTip(tip);
        item->setSizeHint(kThumb + QSize(12, 34));
        if (dim) item->setForeground(QColor("#6c7086"));
        if (icons_.contains(key)) item->setIcon(icons_.value(key));
        grid_->addItem(item);
        if (key == selected) grid_->setCurrentItem(item);
    };
    if (sources_->currentIndex() == Online) {
        for (const OnlineWallpaper &w : online_)
            add(w.key(), (w.video ? "LIVE  " : "") + w.title,
                w.title + "\n" + OnlineLibrary::sourceName(w.source) + (w.resolution.isEmpty() ? "" : " - " + w.resolution) +
                    (w.credit.isEmpty() ? "" : "\n" + QString(w.source == OnlineSource::Sucrose ? "By " : "") + w.credit),
                false);
    } else {
        for (const Wallpaper &w : local_)
            add(tileKey(w), (w.kind == Kind::Video ? "LIVE  " : "") + w.title, w.title, w.kind == Kind::Unsupported);
    }
    grid_->setUpdatesEnabled(true);
    grid_->verticalScrollBar()->setValue(scroll);
    scrollTarget_ = scroll;
}

void MainWindow::hoverItem(QListWidgetItem *item) {
    int row = grid_->row(item);
    if (row == hoverRow_) return;
    stopHover();
    if (row < 0) return;
    hoverRow_ = row;
    QRect tile = grid_->visualItemRect(item);
    // The icon sits centred at the top of the tile.
    hover_->move(tile.x() + (tile.width() - kThumb.width()) / 2, tile.y() + 4);

    if (sources_->currentIndex() == Online) {
        if (row >= online_.size()) return;
        const OnlineWallpaper &w = online_[row];
        // The whole picture, from its (uncropped) preview, straight away.
        QString thumb = paths::thumbs() + "/" + w.key() + ".jpg";
        if (QFile::exists(thumb)) playHover(thumb, false, true);
        // X Live has no small preview video: only its picture until it is set.
        if (!w.video || w.source == OnlineSource::XLive) return;
        if (!w.previewUrl.isEmpty()) {
            playHover(w.previewUrl, true, false);
        } else {
            hoverKey_ = w.key();
            onlineLib_.resolve(w);
        }
        return;
    }
    if (row >= local_.size()) return;
    const Wallpaper &w = local_[row];
    if (w.kind == Kind::Video) {
        QString still = videoThumbnail(w.file, false);
        if (!still.isEmpty()) playHover(still, false, true);
        playHover(w.file, true, true);
    }
    else if (w.kind == Kind::Gif || w.kind == Kind::Image)
        playHover(w.file, false, true);
    else if (!w.preview.isEmpty())
        playHover(w.preview, false, true);
}

void MainWindow::playHover(const QString &url, bool video, bool local) {
    hoverKey_.clear();
    if (video) {
        // Started a moment later, so sweeping the mouse across the grid
        // does not start (and stop) a video for every tile it passes.
        hoverVideoUrl_ = url;
        hoverVideoLocal_ = local;
        hoverDelay_->start();
    } else {
        hover_->setCurrentWidget(hoverLabel_);
        QSize size = QImageReader(url).size();
        QSize fitted = size.isValid() ? size.scaled(kThumb, Qt::KeepAspectRatio) : kThumb;
        if (QImageReader(url).supportsAnimation() && QImageReader(url).imageCount() != 1) {
            hoverMovie_ = new QMovie(url, QByteArray(), this);
            hoverMovie_->setScaledSize(fitted);
            hoverLabel_->setMovie(hoverMovie_);
            hoverMovie_->start();
        } else {
            QImageReader reader(url);
            reader.setScaledSize(fitted);
            hoverLabel_->setPixmap(QPixmap::fromImage(reader.read()));
        }
    }
    hover_->show();
    hover_->raise();
}

void MainWindow::stopHover() {
    hoverRow_ = -1;
    hoverKey_.clear();
    hoverVideoUrl_.clear();
    hoverDelay_->stop();
    hover_->hide();
    hover_->setCurrentWidget(hoverLabel_);
    if (hoverPlayer_->playbackState() != QMediaPlayer::StoppedState) hoverPlayer_->stop();
    hoverLabel_->clear();
    delete hoverMovie_;
    hoverMovie_ = nullptr;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == grid_->viewport()) {
        if (event->type() == QEvent::Wheel) {
            auto *wheel = static_cast<QWheelEvent *>(event);
            // Touchpads already scroll smoothly; animate mouse wheel notches.
            if (wheel->pixelDelta().isNull() && wheel->angleDelta().y() != 0) {
                QScrollBar *bar = grid_->verticalScrollBar();
                if (scrollAnimation_->state() != QAbstractAnimation::Running) scrollTarget_ = bar->value();
                scrollTarget_ = qBound(bar->minimum(), scrollTarget_ - wheel->angleDelta().y() * 3 / 2, bar->maximum());
                scrollAnimation_->stop();
                scrollAnimation_->setStartValue(bar->value());
                scrollAnimation_->setEndValue(scrollTarget_);
                scrollAnimation_->start();
                return true;
            }
        }
        if (event->type() == QEvent::Leave) stopHover();
        // Moving onto empty space between tiles.
        if (event->type() == QEvent::MouseMove && !grid_->itemAt(static_cast<QMouseEvent *>(event)->pos()))
            stopHover();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::loadThumb(const QString &key, const QString &path, bool video) {
    QThreadPool::globalInstance()->start([this, key, path, video] {
        QString source = video ? videoThumbnail(path) : path;
        QImage image = source.isEmpty() ? QImage() : scaledImage(source);
        if (image.isNull()) return;
        QMetaObject::invokeMethod(this, [this, key, image] {
            QIcon icon(QPixmap::fromImage(image));
            icons_.insert(key, icon);
            for (int i = 0; i < grid_->count(); ++i)
                if (grid_->item(i)->data(Qt::UserRole).toString() == key) grid_->item(i)->setIcon(icon);
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
        bool have = QFile::exists(OnlineLibrary::localFile(w));
        QStringList parts = {OnlineLibrary::sourceName(w.source), w.video ? "Live" : "Picture"};
        if (!w.resolution.isEmpty()) parts << w.resolution;
        if (w.fileSize > 0) parts << humanSize(w.fileSize);
        parts << (have ? "already downloaded" : "downloaded when you set it");
        if (!w.credit.isEmpty()) parts << (w.source == OnlineSource::Sucrose ? "by " + w.credit : w.credit);
        details_->setText(parts.join(" - "));
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
        const OnlineWallpaper &w = online_[row];
        if (w.fullUrl.isEmpty()) {
            // NASA and X Live: look up the file first, then set it. A failed
            // look-up drops the wait too, so a later Set does not download twice.
            QString key = w.key();
            auto connection = std::make_shared<QMetaObject::Connection>();
            auto failure = std::make_shared<QMetaObject::Connection>();
            *connection = connect(&onlineLib_, &OnlineLibrary::resolved, this, [this, key, connection, failure] {
                for (const OnlineWallpaper &o : online_)
                    if (o.key() == key && !o.fullUrl.isEmpty()) {
                        disconnect(*connection);
                        disconnect(*failure);
                        onlineLib_.download(o);
                        return;
                    }
            });
            *failure = connect(&onlineLib_, &OnlineLibrary::failed, this, [connection, failure] {
                disconnect(*connection);
                disconnect(*failure);
            });
            onlineLib_.resolve(w);
        } else {
            onlineLib_.download(w);
        }
        applyButton_->setEnabled(false);
        return;
    }
    if (row >= local_.size() || local_[row].kind == Kind::Unsupported) return;
    apply(local_[row].file, local_[row].title);
}

bool MainWindow::matchesSearch(const Wallpaper &w) const {
    QStringList words = search_->text().toLower().split(QRegularExpression("[^a-z0-9]+"), Qt::SkipEmptyParts);
    if (words.isEmpty()) return true;
    // Name, file name and (for Wallpaper Engine) its tags and description;
    // every word has to match, in either singular or plural.
    QString text = (w.title + " " + QFileInfo(w.file).fileName() + " " + w.tags.join(' ')).toLower();
    for (const QString &word : words) {
        bool found = false;
        for (const QString &v : OnlineLibrary::variants(word)) found |= text.contains(v);
        if (!found) return false;
    }
    return true;
}

void MainWindow::updateSourcesLabel() {
    int on = 0;
    for (QCheckBox *box : sourceBoxes_) on += box->isChecked();
    sourcesButton_->setText(on == sourceBoxes_.size() ? "All sources" : QString("Sources: %1").arg(on));
}

void MainWindow::searchOnline(bool more) {
    if (sources_->currentIndex() != Online) {
        refreshLibrary();
        return;
    }
    QString query = search_->text().trimmed();
    if (OnlineLibrary::blocked(query)) {
        ++generation_;
        grid_->clear();
        online_.clear();
        arriving_.clear();
        moreButton_->hide();
        libraryNote_->setText("That search is not allowed.");
        return;
    }
    if (!more) {
        stopHover();
        ++generation_;
        grid_->clear();
        online_.clear();
        arriving_.clear();
        libraryNote_->setText("Loading...");
    }
    SearchOptions options;
    options.query = query;
    options.filter = OnlineFilter(filter_->currentData().toInt());
    options.genre = Genre(genre_->currentData().toInt());
    for (QCheckBox *box : sourceBoxes_)
        if (box->isChecked()) options.sources << OnlineSource(box->property("source").toInt());
    options.strict = strict_->isChecked();
    options.hideAi = hideAi_->isChecked();
    options.resolution = Resolution(resolution_->currentData().toInt());
    if (options.sources.isEmpty()) {
        libraryNote_->setText("Tick at least one source.");
        return;
    }
    onlineLib_.search(options, more);
}

// --- Upload page --------------------------------------------------------------

QWidget *MainWindow::buildUploadPage() {
    auto *page = new QWidget;
    auto *col = new QVBoxLayout(page);
    col->setContentsMargins(28, 24, 28, 24);
    col->addWidget(heading("Upload wallpaper"));
    auto *intro = new QLabel("Add your own images (PNG, JPG, WebP, BMP), animated GIFs or videos (MP4, WebM, MKV, "
                             "MOV), from your files or from links. They are copied into your library.");
    intro->setObjectName("dim");
    intro->setWordWrap(true);
    col->addWidget(intro);

    auto *drop = new QLabel("Drop files here, or a video or picture dragged from your browser");
    drop->setObjectName("drop");
    drop->setAlignment(Qt::AlignCenter);
    drop->setMinimumHeight(220);
    col->addWidget(drop);

    auto *choose = new QPushButton("Choose files...");
    choose->setObjectName("primary");
    col->addWidget(choose, 0, Qt::AlignLeft);
    // From links: straight to a file, or a page with one in it.
    auto *linksLabel = new QLabel("Or paste links, one per line: a link to a video, GIF or picture file, or to a "
                                  "page that has one.");
    linksLabel->setObjectName("dim");
    linksLabel->setWordWrap(true);
    col->addSpacing(12);
    col->addWidget(linksLabel);
    linksBox_ = new QPlainTextEdit;
    linksBox_->setPlaceholderText("https://cdn.artstation.com/p/video_sources/.../wallpaper.mp4");
    linksBox_->setFixedHeight(84);
    col->addWidget(linksBox_);
    linksButton_ = new QPushButton("Add from links");
    linksCancel_ = new QPushButton("Cancel");
    linksCancel_->hide();
    auto *linksRow = new QHBoxLayout;
    linksRow->addWidget(linksButton_);
    linksRow->addWidget(linksCancel_);
    linksRow->addStretch();
    col->addLayout(linksRow);
    connect(linksCancel_, &QPushButton::clicked, &links_, &LinkImporter::cancel);
    uploadResult_ = new QLabel;
    uploadResult_->setWordWrap(true);
    uploadResult_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    col->addWidget(uploadResult_);
    col->addStretch();

    connect(linksButton_, &QPushButton::clicked, this, [this] {
        QStringList links = linksInText(linksBox_->toPlainText());
        if (links.isEmpty()) {
            uploadResult_->setText("No http or https links in the box.");
            return;
        }
        linksBox_->clear();
        addLinks(links);
    });
    connect(&links_, &LinkImporter::progress, this, [this](const QString &link, qint64 received, qint64 total) {
        QString amount = total > 0 ? humanSize(received) + " of " + humanSize(total) : humanSize(received);
        uploadResult_->setText("Downloading " + link + " ... " + amount);
    });
    connect(&links_, &LinkImporter::finished, this, [this](const QString &link, const QString &file,
                                                           const QString &error) {
        if (file.isEmpty()) {
            linkErrors_ << link + ": " + error;
            return;
        }
        QString importError;
        QString copy = importFile(file, importError, true);
        QFile::remove(file);
        if (copy.isEmpty())
            linkErrors_ << link + ": " + importError;
        else
            linkAdded_ << QFileInfo(copy).fileName();
    });
    connect(&links_, &LinkImporter::allDone, this, [this] {
        linksButton_->setEnabled(true);
        linksCancel_->hide();
        QString text;
        if (!linkAdded_.isEmpty())
            text += "Added: " + linkAdded_.join(", ") + ". Find them under Library > My wallpapers.";
        if (!linkErrors_.isEmpty()) text += (text.isEmpty() ? "" : "\n") + linkErrors_.join("\n");
        uploadResult_->setText(text);
        linkAdded_.clear();
        linkErrors_.clear();
        if (sources_->currentIndex() == Mine) refreshLibrary();
    });
    connect(choose, &QPushButton::clicked, this, [this] {
        QStringList files = QFileDialog::getOpenFileNames(
            this, "Choose wallpapers", QDir::homePath(),
            "Wallpapers (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.mp4 *.webm *.mkv *.mov *.avi *.m4v)");
        if (!files.isEmpty()) upload(files);
    });
    return page;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    // Files from the file manager; links (a video or picture dragged from a
    // browser, or a link's text) are downloaded.
    QStringList files, links;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            files << url.toLocalFile();
        else if (url.scheme() == "http" || url.scheme() == "https")
            links << url.toString();
    }
    if (files.isEmpty() && links.isEmpty() && event->mimeData()->hasText())
        links = linksInText(event->mimeData()->text());
    if (files.isEmpty() && links.isEmpty()) return;
    nav_->setCurrentRow(2);
    if (!files.isEmpty()) upload(files);
    if (!links.isEmpty()) addLinks(links);
}

void MainWindow::addLinks(const QStringList &links) {
    linksButton_->setEnabled(false);
    linksCancel_->show();
    uploadResult_->setText(QString("Loading %1 link%2...").arg(links.size()).arg(links.size() == 1 ? "" : "s"));
    links_.add(links);
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

// --- Widgets page -------------------------------------------------------------

QWidget *MainWindow::buildWidgetsPage() {
    auto *page = new QWidget;
    auto *col = new QVBoxLayout(page);
    col->setContentsMargins(28, 24, 28, 24);
    col->addWidget(heading("Widgets"));
    auto *intro = new QLabel("Shown on top of the wallpaper while it is running. Changes apply straight away.");
    intro->setObjectName("dim");
    col->addWidget(intro);

    auto placeBox = [](Place current) {
        auto *box = new QComboBox;
        for (int i = 0; i < kPlaceCount; ++i) box->addItem(placeName(Place(i)), i);
        box->setCurrentIndex(int(current));
        return box;
    };
    auto sizeSlider = [](int value, int min, int max) {
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange(min, max);
        slider->setValue(value);
        slider->setFixedWidth(220);
        return slider;
    };
    // A button showing a colour; clicking it opens a colour picker.
    auto colorButton = [this](QString *target) {
        auto *button = new QPushButton;
        auto paint = [button, target] {
            button->setText(*target);
            button->setStyleSheet("background: " + *target + "; color: " +
                                  (QColor(*target).lightness() > 128 ? "#1e1e2e" : "#ffffff") + ";");
        };
        paint();
        connect(button, &QPushButton::clicked, this, [this, target, paint] {
            QColor picked = QColorDialog::getColor(QColor(*target), this, "Choose a colour");
            if (!picked.isValid()) return;
            *target = picked.name();
            paint();
            saveWidgets();
        });
        return button;
    };

    // Clock.
    auto *clockGroup = new QGroupBox("Clock (Mond style)");
    auto *clockForm = new QFormLayout(clockGroup);
    auto *clockOn = new QCheckBox("Show the clock");
    clockOn->setChecked(settings_.clock);
    auto *clockPlace = placeBox(settings_.clockPlace);
    auto *clockSize = sizeSlider(settings_.clockSize, 30, 300);
    auto *clockFont = new QComboBox;
    clockFontBox_ = clockFont;
    clockFont->setMaxVisibleItems(20);
    auto *clock24 = new QCheckBox("24-hour time");
    clock24->setChecked(settings_.clock24h);
    auto *clockSeconds = new QCheckBox("Show seconds");
    clockSeconds->setChecked(settings_.clockSeconds);
    clockForm->addRow(clockOn);
    clockForm->addRow("Position", clockPlace);
    clockForm->addRow("Size", clockSize);
    clockForm->addRow("Colour", colorButton(&settings_.clockColor));
    clockForm->addRow("Font", clockFont);
    clockForm->addRow(clock24);
    clockForm->addRow(clockSeconds);
    col->addWidget(clockGroup);

    // Music.
    auto *musicGroup = new QGroupBox("Music player");
    auto *musicForm = new QFormLayout(musicGroup);
    auto *musicOn = new QCheckBox("Show what's playing (Spotify first, otherwise any player)");
    musicOn->setChecked(settings_.music);
    auto *musicPlace = placeBox(settings_.musicPlace);
    auto *musicSize = sizeSlider(settings_.musicSize, 50, 250);
    musicForm->addRow(musicOn);
    musicForm->addRow("Position", musicPlace);
    musicForm->addRow("Size", musicSize);
    musicForm->addRow("Colour", colorButton(&settings_.musicColor));
    auto *musicNote = new QLabel("Hidden while nothing is playing. Its buttons work on the desktop, and you can drag it "
                                 "by its card.");
    musicNote->setObjectName("dim");
    musicForm->addRow(musicNote);
    col->addWidget(musicGroup);

    // A song (or a video's sound) that plays with the wallpaper.
    auto *trackGroup = new QGroupBox("Background track");
    auto *trackForm = new QFormLayout(trackGroup);
    auto *trackOn = new QCheckBox("Play a song with the wallpaper (it loops)");
    trackOn->setChecked(settings_.track);
    auto *trackName = new QLabel(settings_.trackTitle.isEmpty() ? QString("No file chosen") : settings_.trackTitle);
    trackName->setWordWrap(true);
    auto *trackChoose = new QPushButton("Choose file...");
    auto *trackRow = new QHBoxLayout;
    trackRow->addWidget(trackName, 1);
    trackRow->addWidget(trackChoose);
    auto *trackVolume = new QSlider(Qt::Horizontal);
    trackVolume->setRange(0, 100);
    trackVolume->setValue(settings_.trackVolume);
    trackForm->addRow(trackOn);
    trackForm->addRow("File", trackRow);
    trackForm->addRow("Volume", trackVolume);
    auto *trackNote = new QLabel("Music or a video (only its sound plays). The Music player above shows it, and its "
                                 "buttons pause it, start it again and seek.");
    trackNote->setObjectName("dim");
    trackNote->setWordWrap(true);
    trackForm->addRow(trackNote);
    col->addWidget(trackGroup);
    connect(trackChoose, &QPushButton::clicked, this, [this, trackName, trackOn] {
        QString file = QFileDialog::getOpenFileName(
            this, "Choose a song or video", QDir::homePath(),
            "Music and videos (*.mp3 *.flac *.ogg *.opus *.wav *.m4a *.aac *.wma *.mp4 *.webm *.mkv *.mov *.avi);;All files (*)");
        if (file.isEmpty()) return;
        // roofce keeps its own copy, so moving or deleting the original is fine.
        QDir dir(paths::tracks());
        for (const QFileInfo &old : dir.entryInfoList(QDir::Files)) QFile::remove(old.absoluteFilePath());
        QFileInfo info(file);
        QString copy = dir.filePath(info.fileName());
        if (!QFile::copy(file, copy)) {
            QMessageBox::warning(this, "Background track", "Could not copy " + info.fileName() + " (is the disk full?)");
            return;
        }
        settings_.trackFile = copy;
        settings_.trackTitle = info.fileName();
        settings_.track = true;
        trackName->setText(info.fileName());
        trackOn->setChecked(true);  // saves too
        saveWidgets();
    });
    connect(trackOn, &QCheckBox::toggled, this, [this](bool on) {
        settings_.track = on;
        saveWidgets();
    });
    connect(trackVolume, &QSlider::sliderReleased, this, [this, trackVolume] {
        settings_.trackVolume = trackVolume->value();
        saveWidgets();
    });

    auto *arrangeRow = new QHBoxLayout;
    auto *reset = new QPushButton("Reset positions");
    auto *arrangeNote = new QLabel("Drag the clock (by its text) and the player (by its card) anywhere on the "
                                   "desktop; Meta+D shows it. They stay where you drop them.");
    arrangeNote->setObjectName("dim");
    arrangeNote->setWordWrap(true);
    arrangeRow->addWidget(arrangeNote, 1);
    arrangeRow->addWidget(reset);
    col->addLayout(arrangeRow);
    col->addStretch();
    connect(reset, &QPushButton::clicked, this, [this] {
        Settings::clearPosition("clock");
        Settings::clearPosition("music");
        saveWidgets();
    });

    auto save = [=] {
        settings_.clock = clockOn->isChecked();
        settings_.clockPlace = Place(clockPlace->currentData().toInt());
        settings_.clockSize = clockSize->value();
        settings_.clockFont = clockFont->currentData().toString();
        settings_.clock24h = clock24->isChecked();
        settings_.clockSeconds = clockSeconds->isChecked();
        settings_.music = musicOn->isChecked();
        settings_.musicPlace = Place(musicPlace->currentData().toInt());
        settings_.musicSize = musicSize->value();
        saveWidgets();
    };
    for (QCheckBox *box : {clockOn, clock24, clockSeconds, musicOn}) connect(box, &QCheckBox::toggled, this, save);
    connect(clockFont, &QComboBox::currentIndexChanged, this, save);
    // Picking a position puts the widget back there (forgets the dragged spot).
    connect(clockPlace, &QComboBox::currentIndexChanged, this, [save] {
        Settings::clearPosition("clock");
        save();
    });
    connect(musicPlace, &QComboBox::currentIndexChanged, this, [save] {
        Settings::clearPosition("music");
        save();
    });
    // Sizes apply when the slider is let go, not on every step.
    for (QSlider *slider : {clockSize, musicSize}) connect(slider, &QSlider::sliderReleased, this, save);
    return page;
}

void MainWindow::saveWidgets() {
    settings_.save();
    if (wallpaperRunning()) {
        QString error;
        showWallpaper(error);
    }
}

// --- Font library and saved fonts -------------------------------------------------

QWidget *MainWindow::buildFontLibraryPage() {
    auto *page = new QWidget;
    auto *col = new QVBoxLayout(page);
    col->setContentsMargins(28, 24, 28, 24);
    col->addWidget(heading("Font library"));
    auto *intro = new QLabel("Popular free fonts for the clock. Downloaded fonts are installed for you, so other apps "
                             "can use them too.");
    intro->setObjectName("dim");
    intro->setWordWrap(true);
    col->addWidget(intro);
    fontSearch_ = new QLineEdit;
    fontSearch_->setPlaceholderText("Search fonts");
    col->addWidget(fontSearch_);

    auto *row = new QHBoxLayout;
    fontList_ = new QListWidget;
    fontList_->setObjectName("grid");
    fontList_->setFixedWidth(330);
    row->addWidget(fontList_);
    auto *side = new QVBoxLayout;
    fontPreview_ = new QLabel;
    fontPreview_->setAlignment(Qt::AlignCenter);
    fontPreview_->setMinimumHeight(220);
    fontPreview_->setWordWrap(true);
    fontPreview_->setStyleSheet("background: #181825; border-radius: 10px;");
    side->addWidget(fontPreview_);
    fontNote_ = new QLabel;
    fontNote_->setObjectName("dim");
    fontNote_->setWordWrap(true);
    side->addWidget(fontNote_);
    auto *buttons = new QHBoxLayout;
    fontDownload_ = new QPushButton("Download");
    fontDownload_->setObjectName("primary");
    fontUse_ = new QPushButton("Use for the clock");
    buttons->addWidget(fontDownload_);
    buttons->addWidget(fontUse_);
    buttons->addStretch();
    side->addLayout(buttons);

    auto *howTo = new QLabel(
        "<b>How to install fonts</b>"
        "<ol style='margin-left:-20px'>"
        "<li><b>From this list:</b> pick a font and click <b>Download</b>. It is installed for you straight away "
        "(in ~/.local/share/fonts/catberry), so every app on this computer can use it.</li>"
        "<li><b>Anurati</b> and fonts from other sites: click <b>Get it in the browser</b> (or find the font "
        "online), download it (a .ttf, .otf or a .zip), then open <b>Saved fonts</b>, click "
        "<b>Import fonts...</b> and choose the file.</li>"
        "<li><b>Put it on the clock:</b> select the font and click <b>Use for the clock</b>, or choose it under "
        "<b>Widgets &gt; Font</b>. Saved fonts are listed first there, marked with a star.</li>"
        "<li>Other apps that were already open may need a restart before they list a new font.</li>"
        "</ol>");
    howTo->setWordWrap(true);
    howTo->setTextFormat(Qt::RichText);
    howTo->setStyleSheet("background: #181825; border-radius: 10px; padding: 10px; color: #a6adc8;");
    side->addWidget(howTo);
    side->addStretch();
    row->addLayout(side, 1);
    col->addLayout(row, 1);

    auto selected = [this]() -> const FontEntry * {
        QListWidgetItem *item = fontList_->currentItem();
        if (!item || item->data(Qt::UserRole).isNull()) return nullptr;
        return &fontCatalog()[item->data(Qt::UserRole).toInt()];
    };
    connect(fontList_, &QListWidget::currentRowChanged, this, [this, selected] {
        const FontEntry *font = selected();
        fontDownload_->setEnabled(font != nullptr);
        fontUse_->setEnabled(font != nullptr);
        if (!font) return;
        bool saved = FontLibrary::isSaved(font->family);
        fontDownload_->setText(!font->webUrl.isEmpty() ? "Get it in the browser"
                               : saved                ? "Download again"
                                                      : "Download");
        fontUse_->setEnabled(saved);
        fontNote_->setText(font->family + " - " + font->license + (saved ? " - saved" : ""));
        if (saved) {
            QFont preview(font->family);
            preview.setPixelSize(40);
            preview.setLetterSpacing(QFont::AbsoluteSpacing, 8);
            fontPreview_->setFont(preview);
            fontPreview_->setText("M O N D A Y\n- 20:45 -");
        } else {
            fontPreview_->setFont(QFont());
            fontPreview_->setText("Download " + font->family + " to see it here");
        }
    });
    connect(fontDownload_, &QPushButton::clicked, this, [this, selected] {
        const FontEntry *font = selected();
        if (!font) return;
        if (!font->webUrl.isEmpty()) {
            QDesktopServices::openUrl(QUrl(font->webUrl));
            fontNote_->setText("Download " + font->family + " (" + font->license.toLower() + ") in the browser, then "
                               "add the file (or its .zip) with Import fonts... on the Saved fonts page.");
            return;
        }
        fontDownload_->setEnabled(false);
        fontNote_->setText("Downloading " + font->family + "...");
        fontLib_.download(*font);
    });
    connect(fontUse_, &QPushButton::clicked, this, [this, selected] {
        if (const FontEntry *font = selected()) useFontForClock(font->family);
    });
    connect(fontSearch_, &QLineEdit::textChanged, this, &MainWindow::refreshFonts);
    return page;
}

QWidget *MainWindow::buildSavedFontsPage() {
    auto *page = new QWidget;
    auto *col = new QVBoxLayout(page);
    col->setContentsMargins(28, 24, 28, 24);
    col->addWidget(heading("Saved fonts"));
    auto *intro = new QLabel("Fonts you downloaded from the Font library or imported. To add one: download it in "
                             "your browser, click Import fonts... below and choose the .ttf, .otf, or the .zip it "
                             "came in. It is installed for you, and other apps can use it too.");
    intro->setWordWrap(true);
    intro->setObjectName("dim");
    col->addWidget(intro);
    savedList_ = new QListWidget;
    savedList_->setObjectName("grid");
    col->addWidget(savedList_, 1);
    savedNote_ = new QLabel;
    savedNote_->setObjectName("dim");
    savedNote_->setWordWrap(true);
    col->addWidget(savedNote_);
    auto *buttons = new QHBoxLayout;
    auto *import = new QPushButton("Import fonts...");
    import->setObjectName("primary");
    auto *use = new QPushButton("Use for the clock");
    auto *remove = new QPushButton("Remove");
    buttons->addWidget(import);
    buttons->addWidget(use);
    buttons->addWidget(remove);
    buttons->addStretch();
    col->addLayout(buttons);

    connect(import, &QPushButton::clicked, this, [this] {
        QStringList files = QFileDialog::getOpenFileNames(this, "Import fonts", QDir::homePath(),
                                                          "Fonts (*.ttf *.otf *.ttc *.zip *.TTF *.OTF *.ZIP)");
        if (files.isEmpty()) return;
        QString error;
        QStringList added = fontLib_.import(files, error);
        savedNote_->setText((added.isEmpty() ? QString() : "Added: " + added.join(", ") + "\n") + error.trimmed());
        refreshFonts();
    });
    connect(use, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem *item = savedList_->currentItem()) useFontForClock(item->text());
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        QListWidgetItem *item = savedList_->currentItem();
        if (!item) return;
        QString family = item->text();
        if (QMessageBox::question(this, "Remove font", "Remove " + family + " from your saved fonts?") !=
            QMessageBox::Yes)
            return;
        fontLib_.remove(family);
        if (settings_.clockFont == family) useFontForClock(QString());
        savedNote_->setText(family + " removed.");
        refreshFonts();
    });
    return page;
}

void MainWindow::refreshFonts() {
    QStringList saved = FontLibrary::savedFamilies();
    QString filter = fontSearch_->text().trimmed();

    // Font library: suggested, then the rest.
    int current = fontList_->currentItem() ? fontList_->currentItem()->data(Qt::UserRole).toInt() : -1;
    fontList_->clear();
    bool headerShown[2] = {false, false};
    for (int i = 0; i < fontCatalog().size(); ++i) {
        const FontEntry &font = fontCatalog()[i];
        if (!filter.isEmpty() && !font.family.contains(filter, Qt::CaseInsensitive)) continue;
        int group = font.suggested ? 0 : 1;
        if (!headerShown[group]) {
            headerShown[group] = true;
            auto *header = new QListWidgetItem(group == 0 ? "Suggested fonts" : "More popular fonts");
            header->setFlags(Qt::NoItemFlags);
            QFont bold = header->font();
            bold.setBold(true);
            header->setFont(bold);
            header->setForeground(QColor("#89b4fa"));
            fontList_->addItem(header);
        }
        bool isSaved = saved.contains(font.family, Qt::CaseInsensitive);
        auto *item = new QListWidgetItem((isSaved ? "\u2713  " : "     ") + font.family);
        item->setData(Qt::UserRole, i);
        if (isSaved) {
            QFont preview(font.family);
            preview.setPointSize(13);
            item->setFont(preview);
        }
        fontList_->addItem(item);
        if (i == current) fontList_->setCurrentItem(item);
    }

    // Saved fonts, each shown in itself.
    savedList_->clear();
    for (const QString &family : saved) {
        auto *item = new QListWidgetItem(family);
        QFont preview(family);
        preview.setPointSize(16);
        item->setFont(preview);
        savedList_->addItem(item);
    }
    if (saved.isEmpty() && savedNote_->text().isEmpty())
        savedNote_->setText("No saved fonts yet: download some in the Font library, or import your own.");

    // The clock's font list: automatic, saved fonts, then all the others.
    if (clockFontBox_) {
        QSignalBlocker block(clockFontBox_);
        clockFontBox_->clear();
        clockFontBox_->addItem("Automatic (Anurati if saved, else a thin font)", QString());
        for (const QString &family : saved) clockFontBox_->addItem("\u2605 " + family, family);
        for (const QString &family : QFontDatabase::families())
            if (!saved.contains(family)) clockFontBox_->addItem(family, family);
        clockFontBox_->setCurrentIndex(qMax(0, clockFontBox_->findData(settings_.clockFont)));
    }
}

void MainWindow::useFontForClock(const QString &family) {
    settings_.clockFont = family;
    if (!family.isEmpty()) settings_.clock = true;
    saveWidgets();
    refreshFonts();
    if (!family.isEmpty()) {
        fontNote_->setText("The clock now uses " + family + ".");
        savedNote_->setText("The clock now uses " + family + ".");
    }
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

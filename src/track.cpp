#include "track.h"

#include <QAudioOutput>
#include <QFileInfo>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QUrl>

TrackPlayer::TrackPlayer(QObject *parent) : QObject(parent) {
    player_ = new QMediaPlayer(this);
    audio_ = new QAudioOutput(this);
    // Only an audio output: a video's picture isn't even decoded.
    player_->setAudioOutput(audio_);
    player_->setLoops(QMediaPlayer::Infinite);
    connect(player_, &QMediaPlayer::metaDataChanged, this, [this] {
        readTags();
        emit changed();
    });
    connect(player_, &QMediaPlayer::playbackStateChanged, this, &TrackPlayer::changed);
    connect(player_, &QMediaPlayer::durationChanged, this, &TrackPlayer::changed);
}

TrackPlayer::~TrackPlayer() { player_->stop(); }

void TrackPlayer::setFile(const QString &file) {
    if (file == file_) return;
    file_ = file;
    title_ = QFileInfo(file).completeBaseName();
    artist_.clear();
    cover_ = QImage();
    player_->setSource(QUrl::fromLocalFile(file));
    player_->play();
    emit changed();
}

void TrackPlayer::setVolume(int percent) { audio_->setVolume(qBound(0, percent, 100) / 100.0f); }

void TrackPlayer::readTags() {
    QMediaMetaData meta = player_->metaData();
    QString title = meta.stringValue(QMediaMetaData::Title).trimmed();
    if (!title.isEmpty()) title_ = title;
    QString artist = meta.stringValue(QMediaMetaData::ContributingArtist).trimmed();
    if (artist.isEmpty()) artist = meta.stringValue(QMediaMetaData::AlbumArtist).trimmed();
    if (artist.isEmpty()) artist = meta.stringValue(QMediaMetaData::Author).trimmed();
    artist_ = artist;
    QImage cover = meta.value(QMediaMetaData::CoverArtImage).value<QImage>();
    if (!cover.isNull()) cover_ = cover;
}

bool TrackPlayer::playing() const { return player_->playbackState() == QMediaPlayer::PlayingState; }
qint64 TrackPlayer::positionUs() const { return player_->position() * 1000; }
qint64 TrackPlayer::lengthUs() const { return player_->duration() * 1000; }

void TrackPlayer::playPause() {
    if (playing())
        player_->pause();
    else
        player_->play();
}

// One song: "next" and "previous" both start it again.
void TrackPlayer::restart() {
    player_->setPosition(0);
    player_->play();
}

void TrackPlayer::seek(double fraction) {
    if (player_->duration() > 0) player_->setPosition(qint64(qBound(0.0, fraction, 1.0) * player_->duration()));
    emit changed();
}

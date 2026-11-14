// The song that plays with the wallpaper: an imported audio file, or a
// video's sound (its picture isn't decoded). It loops, starts with the
// wallpaper, and the Music widget shows and controls it.
#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class QAudioOutput;
class QMediaPlayer;

class TrackPlayer : public QObject {
    Q_OBJECT
public:
    explicit TrackPlayer(QObject *parent = nullptr);
    ~TrackPlayer() override;

    // Plays `file` from the start (nothing changes if it's already the one playing).
    void setFile(const QString &file);
    QString file() const { return file_; }
    void setVolume(int percent);

    // From the file's tags; the title falls back to the file name.
    QString title() const { return title_; }
    QString artist() const { return artist_; }
    QImage cover() const { return cover_; }
    bool playing() const;
    qint64 positionUs() const;
    qint64 lengthUs() const;

    void playPause();
    void restart();
    void seek(double fraction);

signals:
    void changed();

private:
    void readTags();

    QMediaPlayer *player_;
    QAudioOutput *audio_;
    QString file_, title_, artist_;
    QImage cover_;
};

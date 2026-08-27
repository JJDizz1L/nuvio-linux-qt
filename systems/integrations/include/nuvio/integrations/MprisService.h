#pragma once

// MPRIS D-Bus media-control surface (plan P3 - a NEW capability the
// Compose line never had). Exposes:
//   org.mpris.MediaPlayer2            (identity / supported schemes)
//   org.mpris.MediaPlayer2.Player     (transport + metadata + position)
// on the session bus as org.mpris.MediaPlayer2.nuviolinux, so desktop
// media keys, `playerctl`, and DE shell widgets can drive/observe the
// single mpv core through the same queued command surface everything else
// uses. Pure glue: no policy decisions live here.

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QDBusContext>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace nuvio::mpv { class MpvController; }
namespace nuvio::playback { class PlaybackSession; }

namespace nuvio::integrations {

class MprisRootAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes CONSTANT)

public:
    explicit MprisRootAdaptor(QObject* parent);
    bool        canQuit() const;
    bool        canRaise() const;
    bool        hasTrackList() const;
    QString     identity() const;
    QString     desktopEntry() const;
    QStringList supportedUriSchemes() const;
    QStringList supportedMimeTypes() const;


public slots:
    void Quit();
    void Raise();
};

class MprisPlayerAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qint64 Position READ positionUs)
    Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canGoNext CONSTANT)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious CONSTANT)
    Q_PROPERTY(bool CanPlay READ canPlay CONSTANT)
    Q_PROPERTY(bool CanPause READ canPause CONSTANT)
    Q_PROPERTY(bool CanSeek READ canSeek CONSTANT)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)

public:
    explicit MprisPlayerAdaptor(QObject* parent);

    // Controller/session bridges are raw pointers by design: the service is
    // constructed in main() with explicit wiring and torn down first.
    nuvio::mpv::MpvController*       controller  = nullptr;
    nuvio::playback::PlaybackSession* session     = nullptr;

    QString   playbackStatus() const;
    QString   loopStatus() const;
    void      setLoopStatus(const QString&);
    double    rate() const;
    void      setRate(double r);
    bool      shuffle() const;
    void      setShuffle(bool s);
    QVariantMap metadata() const;
    double    volume() const;
    void      setVolume(double v);            // 0..1.3 -> percent
    qint64    positionUs() const;             // mpv us contract
    static constexpr double kMinRate = 1.0;
    static constexpr double kMaxRate = 1.0;
    double minimumRate() const { return kMinRate; }
    double maximumRate() const { return kMaxRate; }
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const;
    bool canControl() const;

signals:
    /// Emitted after external relative seeks (mpris contract).
    void Seeked(qint64 positionUs);

public slots:
    void Play();
    void Pause();
    void Toggle();
    void Stop();
    void Seek(qint64 offsetUs);
    void SetPosition(const QDBusObjectPath& trackId, qint64 positionUs);
    void OpenUri(const QString& uri);
};

class MprisService final : public QObject {
    Q_OBJECT
public:
    MprisService(nuvio::mpv::MpvController* controller,
                 nuvio::playback::PlaybackSession* session,
                 QObject* parent = nullptr);
    bool start();   ///< registers on the session bus; false when unavailable

private:
    QObject* m_root    = nullptr;
    QObject* m_player  = nullptr;
};

} // namespace nuvio::integrations
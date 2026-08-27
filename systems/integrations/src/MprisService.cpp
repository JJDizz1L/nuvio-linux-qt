#include "nuvio/integrations/MprisService.h"

#include "nuvio/mpv/MpvController.h"
#include "nuvio/playback/PlaybackSession.h"

#include <QDBusConnection>
#include <cstdio>
#include <QCoreApplication>

namespace nuvio::integrations {

// ---- root adaptor -----------------------------------------------------------

MprisRootAdaptor::MprisRootAdaptor(QObject* parent)
    : QDBusAbstractAdaptor(parent) {}
bool        MprisRootAdaptor::canQuit() const { return true; }
bool        MprisRootAdaptor::canRaise() const { return false; }
bool        MprisRootAdaptor::hasTrackList() const { return false; }
QString     MprisRootAdaptor::identity() const { return QStringLiteral("Nuvio Linux"); }
QString     MprisRootAdaptor::desktopEntry() const { return QStringLiteral("nuvio-linux"); }
QStringList MprisRootAdaptor::supportedUriSchemes() const
{ return {QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("file")}; }
QStringList MprisRootAdaptor::supportedMimeTypes() const
{
    return {QStringLiteral("video/mp4"), QStringLiteral("video/x-matroska"),
            QStringLiteral("application/vnd.apple.mpegurl")};
}
void MprisRootAdaptor::Quit() { QCoreApplication::quit(); }
void MprisRootAdaptor::Raise() {}

// ---- player adaptor ----------------------------------------------------------

MprisPlayerAdaptor::MprisPlayerAdaptor(QObject* parent)
    : QDBusAbstractAdaptor(parent) {}

QString MprisPlayerAdaptor::playbackStatus() const
{
    const auto s = controller->snapshot();
    if (!s.hasMedia()) return QStringLiteral("Stopped");
    return s.paused ? QStringLiteral("Paused")
                    : QStringLiteral("Playing");
}

QString MprisPlayerAdaptor::loopStatus() const
{ return QStringLiteral("None"); }
void MprisPlayerAdaptor::setLoopStatus(const QString&) {}
double  MprisPlayerAdaptor::rate() const
{ return controller->snapshot().speed; }
void    MprisPlayerAdaptor::setRate(double) {}   // rates locked at 1.0 (W2)
bool    MprisPlayerAdaptor::shuffle() const { return false; }
void    MprisPlayerAdaptor::setShuffle(bool) {}

double MprisPlayerAdaptor::volume() const
{ return controller->snapshot().volume / 100.0; }
void   MprisPlayerAdaptor::setVolume(double v)
{ controller->setVolumePercent(v * 100.0); }

qint64 MprisPlayerAdaptor::positionUs() const
{
    const double sec = controller->snapshot().positionSec;
    return sec < 0 ? 0 : static_cast<qint64>(sec * 1e6);
}

QVariantMap MprisPlayerAdaptor::metadata() const
{
    QVariantMap m;
    const auto s = controller->snapshot();
    if (s.durationSec > 0)
        m.insert(QStringLiteral("mpris:length"),
                 static_cast<qint64>(s.durationSec * 1e6));
    m.insert(QStringLiteral("mpris:trackid"),
             QDBusObjectPath(QStringLiteral(
                 "/org/mpris/MediaPlayer2/track/current")));
    const QString title = session ? session->currentTitle() : QString();
    if (!title.isEmpty())
        m.insert(QStringLiteral("xesam:title"), title);
    else
        m.insert(QStringLiteral("xesam:title"),
                 QStringLiteral("Nuvio playback"));
    return m;
}

bool MprisPlayerAdaptor::canGoNext() const { return false; }
bool MprisPlayerAdaptor::canGoPrevious() const { return false; }
bool MprisPlayerAdaptor::canPlay() const { return true; }
bool MprisPlayerAdaptor::canPause() const { return true; }
bool MprisPlayerAdaptor::canSeek() const { return true; }
bool MprisPlayerAdaptor::canControl() const { return true; }


void MprisPlayerAdaptor::Play()
{ controller->setPaused(false); }
void MprisPlayerAdaptor::Pause()
{ controller->setPaused(true); }
void MprisPlayerAdaptor::Toggle()
{ controller->setPaused(!controller->snapshot().paused); }
void MprisPlayerAdaptor::Stop()
{ controller->enqueueCommand({QStringLiteral("stop")}); }

void MprisPlayerAdaptor::Seek(qint64 offsetUs)
{
    const double from = controller->snapshot().positionSec;
    if (from < 0) return;
    const double target = from + static_cast<double>(offsetUs) / 1e6;
    controller->seekToSeconds(target, true);
    emit Seeked(static_cast<qint64>(target * 1e6));
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath&, qint64 positionUs)
{
    controller->seekToSeconds(static_cast<double>(positionUs) / 1e6, true);
    emit Seeked(positionUs);
}

void MprisPlayerAdaptor::OpenUri(const QString& uri)
{ controller->loadFile(uri); }

// ---- service registration ----------------------------------------------------

MprisService::MprisService(nuvio::mpv::MpvController* controller,
                           nuvio::playback::PlaybackSession* session,
                           QObject* parent)
    : QObject(parent)
{
    // Adaptors forward their D-Bus interface THROUGH their parent object:
    // export the parents, never the adaptors themselves.
    auto* rootObj   = new QObject(this);
    auto* playerObj = new QObject(this);

    auto* rootAd = new MprisRootAdaptor(rootObj);
    auto* playAd = new MprisPlayerAdaptor(playerObj);
    playAd->controller = controller;
    playAd->session    = session;

    m_root   = rootObj;
    m_player = playerObj;
}

bool MprisService::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        std::fprintf(stderr, "mpris: no session bus\n");
        return false;
    }
    if (!bus.registerService(QStringLiteral(
            "org.mpris.MediaPlayer2.nuviolinux"))) {
        std::fprintf(stderr, "mpris: service name taken (another instance?)\n");
        return false;
    }
    bus.registerObject(QStringLiteral("/"), m_root);
    bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), m_player);
    std::fprintf(stderr, "mpris: registered on session bus\n");
    return true;
}

} // namespace nuvio::integrations

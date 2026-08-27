#pragma once

// Stream orchestration above the wire layer (parity port of
// startStream()/stats-polling/idle-stop from P2pStreamingEngine.kt).
//
// Sequence per startStream(infoHash): ensure the local TorrServer process is
// up -> POST action=add -> poll action=get until metadata lists files
// (1 s cadence, 15 s deadline; a timeout falls back to file id 1, exactly
// like Compose) -> resolve the file index -> emit streamReady with a
// 127.0.0.1 direct-playable URL -> 1 Hz statsUpdates while active.
//
// Concurrency model: every start bumps an internal generation; late signal
// deliveries for superseded starts are dropped by comparing against the
// CURRENT generation. All completion signals are emitted QUEUED - callers
// can install connections after calling startStream() and never see a
// synchronous re-entrant callback.
//
// stopStream() drops the active torrent and schedules the binary idle-stop
// (60 s), matching the Compose memory-parity rule that a dead session must
// not leave a resident server holding its RAM piece cache.

#include <QTimer>
#include <QObject>
#include <QString>
#include <functional>

#include "nuvio/p2p/TorrServerProtocol.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace nuvio::p2p {

class TorrServerProcess;

class P2pEngine final : public QObject {
    Q_OBJECT
public:
    explicit P2pEngine(TorrServerProcess* binary, QObject* parent = nullptr);

    /// Returns the token used by the completion signals. Invalid hashes are
    /// reported through streamFailed(token, ...) (queued).
    Q_INVOKABLE quint64 startStream(const QString& infoHash,
                                    const QString& filename = QString());

    /// Drop the current torrent; schedule idle binary stop.
    Q_INVOKABLE void stopStream();

    /// Wire the current "Torrent cache size" setting; consulted once per
    /// startStream right after the binary comes up and BEFORE the torrent
    /// is added (Compose parity - store-without-send was a real bug there:
    /// pushing /settings first is what makes the setting real).
    void setCacheSizeProvider(std::function<int()> provider);

    /// GET-modify-POST the RAM piece-cache size against /settings
    /// (call once per freshly started binary; noop on network failure).
    Q_INVOKABLE void applyCacheSettings(int cacheMb);

signals:
    void streamReady(quint64 token, const QString& localUrl);
    void streamFailed(quint64 token, const QString& reason);
    void statsUpdated(quint64 token, qint64 preloadedBytes,
                      qint64 torrentSize, qint64 downloadSpeedBps,
                      int peers, int seeds);

private:
    bool requestWait(QNetworkReply* rep, QByteArray* responseOut);
    bool postJson(const QString& path, const QByteArray& body,
                  QByteArray* responseOut);
    bool getJson(const QString& path, QByteArray* responseOut);
    bool applyCacheSync(int cacheMb);   ///< fetch-modify-post, blocking-bounded
    void beginSession(quint64 gen, const QString& magnet, const QString& filename);
    void awaitMetadata(quint64 gen, const QString& hash, const QString& magnet,
                       const QString& filename, int attemptsLeft);
    void failToken(quint64 gen, const QString& reason);   // queued emit

    TorrServerProcess* m_binary = nullptr;
    QNetworkAccessManager* m_api = nullptr;
    std::function<int()> m_cacheMbProvider;

    quint64 m_generation = 0;
    QString m_activeHash;
    QTimer  m_statsTimer;   ///< 1 Hz while streaming
    QTimer  m_idleTimer;    ///< single-shot 60 s binary stop
};

} // namespace nuvio::p2p
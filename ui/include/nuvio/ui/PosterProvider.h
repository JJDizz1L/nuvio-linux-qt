#pragma once

// Async poster loader for QML `Image { source: "image://poster/<url>" }`.
//
// Plan §8 P1: replaces the placeholder-only path. Fetches https posters
// off-thread via QNetworkAccessManager (its own IO threads; never blocks
// the scene graph), decodes, keeps a SMALL bounded memory cache, and hands
// the engine a QImage through QQuickImageResponse — the official async
// contract. Failures resolve to a neutral placeholder rather than erroring
// the binding.
//
// Skeleton scope: no disk tier, no eviction heuristics beyond FIFO-bound,
// no http->https policy enforcement yet (library system will own URL
// vetting when it exists).

#include <QQuickAsyncImageProvider>

#include <QHash>
#include <QList>
#include <QString>
#include <QVariant>

class QNetworkAccessManager;

namespace nuvio::ui {

class PosterProvider final : public QQuickAsyncImageProvider {
public:
    /// id == full poster URL after "image://poster/". Non-http(s) ids and
    /// empties return the placeholder immediately.
    QQuickImageResponse* requestImageResponse(
        const QString& id, const QSize& requestedSize) override;

// Implementation note: these are engine-internals used ONLY by the reply
// object living in the same .cpp (an anonymous namespace can't be named a
// friend), so they stay undocumented surface by convention.
[[nodiscard]] QImage cached(const QString& url) const;
void store(const QString& url, const QImage& img);
[[nodiscard]] QNetworkAccessManager* nam();   // lazy single instance
    QHash<QString, QImage> m_cache;
    QList<QString>         m_order;          // FIFO eviction ring
    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace nuvio::ui
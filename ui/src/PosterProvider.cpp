#include "nuvio/ui/PosterProvider.h"

#include <cstdio>

#include <QColor>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace nuvio::ui {
namespace {

constexpr int kCacheMax = 64;

QImage makePlaceholder()
{
    QImage ph(4, 3, QImage::Format_RGB32);
    ph.fill(QColor(34, 34, 40));        // matches Theme.background-ish
    return ph;
}

class PosterReply final : public QQuickImageResponse {
public:
    PosterReply(PosterProvider& owner, QString url, QSize want)
        : m_owner(owner), m_url(std::move(url)), m_want(want)
    {
    }

    void start()
    {
        if (const QImage hit = m_owner.cached(m_url); !hit.isNull()) {
            deliver(hit);
            return;
        }
        QNetworkRequest req{m_url};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        // Posters are static CDN objects; a UA keeps CDNs happy.
        req.setRawHeader("User-Agent", "nuvio-linux-qt/0.1");
        auto* rep = m_owner.nam()->get(req);
        connect(rep, &QNetworkReply::finished, this, [this, rep] {
            rep->deleteLater();
            if (rep->error() != QNetworkReply::NoError) {
                std::fprintf(stderr, "poster: %s -> %s\n",
                             qPrintable(m_url),
                             qPrintable(rep->errorString()));

            }
            const QByteArray bytes = rep->readAll();
            QImage img;
            img.loadFromData(bytes);
            if (img.isNull()) { deliver(makePlaceholder()); return; }
            if (m_want.isValid() && m_want.width() > 0 &&
                img.width() > m_want.width() * 2) {
                // Derive height from aspect ourselves: a zero-height
                // requestedSize must NEVER reach scaled() (collapses to a
                // null image - that was the black-poster bug).
                const int w        = m_want.width() * 2;
                const int h        = qMax(1, int(qreal(w) *
                                           qreal(img.height()) /
                                           qMax<qreal>(1, img.width())));
                img = img.scaled(w, h, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
            }
            m_owner.store(m_url, img);
            deliver(img);
        });
    }

    void deliver(QImage img)
    {
                  m_img = std::move(img);
        emit finished();                 // async contract: engine re-queries textureFactory()
    }

    [[nodiscard]] QQuickTextureFactory* textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_img);
    }

private:
    PosterProvider& m_owner;
    QString         m_url;
    QSize           m_want;
    QImage          m_img = makePlaceholder();
};

} // namespace

QQuickImageResponse* PosterProvider::requestImageResponse(
    const QString& id, const QSize& requestedSize)
{
    auto* r = new PosterReply(*this,
                              id.trimmed(), requestedSize);
    const bool httpish = id.startsWith(QLatin1String("http://")) ||
                         id.startsWith(QLatin1String("https://"));
    if (!httpish || id.trimmed().isEmpty()) {
        r->deliver(makePlaceholder());
    } else {
        r->start();
    }
    return r;
}

QImage PosterProvider::cached(const QString& url) const
{
    return m_cache.value(url);
}

void PosterProvider::store(const QString& url, const QImage& img)
{
    if (m_cache.contains(url)) return;
    if (m_order.size() >= kCacheMax) {
        m_cache.remove(m_order.takeFirst());
    }
    m_cache.insert(url, img);
    m_order.append(url);
}

QNetworkAccessManager* PosterProvider::nam()
{
    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    return m_nam;
}

} // namespace nuvio::ui
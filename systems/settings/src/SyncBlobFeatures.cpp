#include "nuvio/settings/SyncBlobFeatures.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::settings {

// JSON-string quoting for plain-string feature values (payload-string
// features): keeps the passthrough store's "value = JSON document" invariant
// so loadAll parses them back to strings and corrupt objects still skip.
[[nodiscard]] QString quoteJsonString(const QString& s)
{
    QString out;
    out.reserve(s.size() + 2);
    out.append(u'"');
    for (const QChar c : s) {
        switch (c.unicode()) {
        case u'"': out.append(QStringLiteral("\\\"")); break;
        case u'\\': out.append(QStringLiteral("\\\\")); break;
        case u'\b': out.append(QStringLiteral("\\b")); break;
        case u'\f': out.append(QStringLiteral("\\f")); break;
        case u'\n': out.append(QStringLiteral("\\n")); break;
        case u'\r': out.append(QStringLiteral("\\r")); break;
        case u'\t': out.append(QStringLiteral("\\t")); break;
        default:
            if (c.unicode() < 0x20)
                out.append(QStringLiteral("\\u%1").arg(
                    static_cast<unsigned int>(c.unicode()), 4, 16,
                    QLatin1Char('0')));
            else
                out.append(c);
        }
    }
    out.append(u'"');
    return out;
}

QJsonObject buildPushBlob(const QJsonObject& playerFragment,
                          const QJsonObject& passthrough)
{
    QJsonObject features;
    for (auto it = passthrough.constBegin(); it != passthrough.constEnd();
         ++it) {
        if (it.key() == QLatin1String(BlobFeature::kPlayer)) continue;
        features.insert(it.key(), it.value());
    }
    features.insert(QLatin1String(BlobFeature::kPlayer), playerFragment);
    return QJsonObject{{QStringLiteral("version"), BlobFeature::kVersion},
                       {QStringLiteral("features"), features}};
}

BlobPassthroughStore::BlobPassthroughStore(int profileId)
    : m_store(std::make_unique<PropertiesStore>(
          PropertiesStore::defaultPath("sync_blob_passthrough"))),
      m_profileId(profileId)
{}

void BlobPassthroughStore::setProfileId(int profileId)
{
    m_profileId = profileId;
}

namespace {
[[nodiscard]] std::string suffixed(const char* name, int profileId)
{
    return std::string(name) + "_" + std::to_string(profileId);
}
[[nodiscard]] std::string suffixed(const QString& name, int profileId)
{
    return name.toStdString() + "_" + std::to_string(profileId);
}
} // namespace

QJsonObject BlobPassthroughStore::loadAll()
{
    // PropertiesStore has no key enumeration; the feature set is closed and
    // tiny, so probe the 13 known serial names (player excluded: owned).
    static const char* const kNames[] = {
        BlobFeature::kTheme,         BlobFeature::kPosterCardStyle,
        BlobFeature::kCardDepthStyle, BlobFeature::kStreamBadges,
        BlobFeature::kDebrid,        BlobFeature::kTmdb,
        BlobFeature::kMdbList,       BlobFeature::kMetaScreen,
        BlobFeature::kCollectionMobile, BlobFeature::kContinueWatching,
        BlobFeature::kTraktSettings, BlobFeature::kTraktComments,
        BlobFeature::kNotifications,
    };
    QJsonObject out;
    for (const char* name : kNames) {
        // NOTE: pre-P7 entries were stored unsuffixed; they orphan on first
        // read here and refresh from the next pull (transient cache only).
        const auto raw = m_store->getString(suffixed(name, m_profileId));
        if (!raw || raw->empty()) continue;
        const QByteArray bytes = QByteArray::fromStdString(*raw);
        // QJsonDocument cannot hold top-level scalars (probe-verified:
        // fromJson("\"s\"") isNull), so parse scalars via an array wrap.
        QJsonDocument doc = QJsonDocument::fromJson(bytes);
        QJsonValue v;
        if (doc.isObject())
            v = doc.object();
        else if (doc.isArray())
            v = doc.array();
        else {
            const QJsonDocument wrapped = QJsonDocument::fromJson(
                QByteArray("[") + bytes + QByteArray("]"));
            if (!wrapped.isArray() || wrapped.array().isEmpty()) continue;
            v = wrapped.array().first();
            if (v.isNull() || v.isUndefined()) continue;
        }
        out.insert(QLatin1String(name), v);
    }
    return out;
}

void BlobPassthroughStore::mergeFromPull(const QJsonObject& received)
{
    for (auto it = received.constBegin(); it != received.constEnd(); ++it) {
        if (it.key() == QLatin1String(BlobFeature::kPlayer)) continue;
        if (it.key() == QLatin1String(BlobFeature::kContinueWatching))
            continue;   // applied into the CW store, not the passthrough
        const QJsonValue v = it.value();
        QString compact;
        if (v.isObject())
            compact = QString::fromUtf8(
                QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
        else if (v.isArray())
            compact = QString::fromUtf8(
                QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
        else if (v.isString())
            compact = quoteJsonString(v.toString());
        else if (v.isBool())
            compact = v.toBool() ? QStringLiteral("true")
                                 : QStringLiteral("false");
        else if (v.isDouble())
            compact = QString::number(v.toDouble());
        else
            continue;   // null/undefined: never cache a wipe vector
        m_store->putString(suffixed(it.key(), m_profileId),
                           compact.toStdString());
    }
}

} // namespace nuvio::settings

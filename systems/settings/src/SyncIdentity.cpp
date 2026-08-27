#include "nuvio/settings/SyncIdentity.h"

#include <QRandomGenerator>

#include <nuvio/settings/PropertiesStore.h>

namespace nuvio::settings {

namespace {
constexpr auto kAlphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
constexpr int  kSuffixLength = 32;
} // namespace

bool SyncIdentity::isValidClientId(const QString& id)
{
    if (id.length() < 16 || id.length() > 96) return false;
    for (const QChar c : id) {
        const char16_t u = c.unicode();
        const bool ok = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') ||
                        (u >= u'0' && u <= u'9') || u == u'-' || u == u'_';
        if (!ok) return false;
    }
    return true;
}

QString SyncIdentity::currentClientId(PropertiesStore& store)
{
    if (const auto stored = store.getString(kKeyName)) {
        const QString id =
            QString::fromStdString(*stored).trimmed();
        if (isValidClientId(id)) return id;
        // Invalid/stale shape: fall through and replace it.
    }

    QString suffix;
    suffix.reserve(kSuffixLength);
    QRandomGenerator* rng = QRandomGenerator::system();
    for (int i = 0; i < kSuffixLength; ++i)
        suffix += QLatin1Char(kAlphabet[rng->bounded(36)]);

    const QString id = QLatin1String(kPrefix) + suffix;
    store.putString(kKeyName, id.toStdString());
    store.persist();
    return id;
}

void SyncIdentity::resetClientId(PropertiesStore& store)
{
    store.remove(kKeyName);
    store.persist();
}

} // namespace nuvio::settings
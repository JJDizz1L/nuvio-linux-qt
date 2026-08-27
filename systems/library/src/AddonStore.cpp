#include "nuvio/library/AddonStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>

#include <nuvio/settings/PropertiesStore.h>

namespace nuvio::library {

using nuvio::settings::PropertiesStore;

namespace {

// Port of AddonTransportUrls.kt encodeUnsafeHttpUrlCharacters.
QString encodeUnsafeUrlChars(const QString& url)
{
    QString out;
    out.reserve(url.size());
    for (const QChar c : url) {
        switch (c.unicode()) {
        case u' ':  out += QStringLiteral("%20"); break;
        case u'"':  out += QStringLiteral("%22"); break;
        case u'<':  out += QStringLiteral("%3C"); break;
        case u'>':  out += QStringLiteral("%3E"); break;
        case u'\\': out += QStringLiteral("%5C"); break;
        case u'^':  out += QStringLiteral("%5E"); break;
        case u'`':  out += QStringLiteral("%60"); break;
        case u'{':  out += QStringLiteral("%7B"); break;
        case u'|':  out += QStringLiteral("%7C"); break;
        case u'}':  out += QStringLiteral("%7D"); break;
        default:
            if (c.unicode() <= 0x1F || c.unicode() == 0x7F) {
                const QByteArray utf8 = QString(c).toUtf8();
                for (const char b : utf8) {
                    out += QLatin1Char('%');
                    out += QString::number(quint8(b), 16)
                               .rightJustified(2, QLatin1Char('0'))
                               .toUpper();
                }
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

QStringList decodeStringArray(const std::optional<std::string>& raw)
{
    QStringList urls;
    if (!raw) return urls;
    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(*raw));
    if (!doc.isArray()) return urls;
    for (const auto& v : doc.array())
        if (v.isString()) urls << v.toString();
    return urls;   // parse errors degrade to empty - Compose getOrNull() parity
}

QByteArray encodeStringArray(const QStringList& urls)
{
    QJsonArray arr;
    for (const auto& u : urls) arr.append(u);
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

AddonStore::EnabledMap decodeEnabledMap(const std::optional<std::string>& raw)
{
    AddonStore::EnabledMap map;
    if (!raw) return map;
    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(*raw));
    if (!doc.isObject()) return map;
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        if (it.value().isBool()) map.insert(it.key(), it.value().toBool());
    return map;
}

QByteArray encodeEnabledMap(const AddonStore::EnabledMap& states)
{
    QJsonObject obj;
    for (auto it = states.cbegin(); it != states.cend(); ++it)
        obj.insert(it.key(), it.value());
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

} // namespace

QString AddonStore::normalizeManifestUrl(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return {};

    QString normalized;
    if (trimmed.startsWith(QLatin1String("http://")) ||
        trimmed.startsWith(QLatin1String("https://"))) {
        normalized = trimmed;
    } else if (trimmed.startsWith(QLatin1String("stremio://"))) {
        normalized = QStringLiteral("https://") +
                     trimmed.mid(int(qstrlen("stremio://")));
    } else {
        normalized = QStringLiteral("https://") + trimmed;
    }

    const QString withoutFragment = normalized.section(QLatin1Char('#'), 0, 0);
    const int queryIdx = withoutFragment.indexOf(QLatin1Char('?'));
    const QString query =
        queryIdx >= 0 ? withoutFragment.mid(queryIdx + 1) : QString();
    QString path = queryIdx >= 0 ? withoutFragment.left(queryIdx)
                                 : withoutFragment;
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);                                       // trimEnd('/')
    if (!path.endsWith(QLatin1String("/manifest.json")))
        path += QStringLiteral("/manifest.json");

    return encodeUnsafeUrlChars(
        query.isEmpty() ? path : path + QLatin1Char('?') + query);
}

namespace {

QString profileKey(const char* base, const int profileId)
{
    return QStringLiteral("%1_%2").arg(QLatin1String(base)).arg(profileId);
}

} // namespace

QStringList AddonStore::loadInstalledUrls(nuvio::settings::PropertiesStore& truth,
                                          const int profileId)
{
    return decodeStringArray(
        truth.getString(profileKey("installed_addon_urls", profileId)
                            .toStdString()));
}

void AddonStore::saveInstalledUrls(PropertiesStore& truth,
                                   const QStringList& urls,
                                   const int profileId)
{
    const QByteArray j = encodeStringArray(urls);
    truth.putString(
        profileKey("installed_addon_urls", profileId).toStdString(),
        std::string(j.constData(), size_t(j.size())));
}

AddonStore::EnabledMap AddonStore::loadEnabledStates(
    nuvio::settings::PropertiesStore& truth, const int profileId)
{
    return decodeEnabledMap(truth.getString(
        profileKey("addon_enabled_states", profileId).toStdString()));
}

void AddonStore::saveEnabledStates(PropertiesStore& truth,
                                   const EnabledMap& states,
                                   const int profileId)
{
    const QByteArray j = encodeEnabledMap(states);
    truth.putString(
        profileKey("addon_enabled_states", profileId).toStdString(),
        std::string(j.constData(), size_t(j.size())));
}

QString AddonStore::cacheKeyFor(const QString& url)
{
    return QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(),
                                   QCryptographicHash::Sha256).toHex());
}

QByteArray AddonStore::loadCachedManifest(
    nuvio::settings::PropertiesStore& cache, const QString& url)
{
    const auto raw = cache.getString(cacheKeyFor(url).toStdString());
    if (!raw) return {};
    return QByteArray::fromStdString(*raw);
}

void AddonStore::saveCachedManifest(PropertiesStore& cache,
                                    const QString& url, const QByteArray& body)
{
    cache.putString(cacheKeyFor(url).toStdString(),
                    std::string(body.constData(), size_t(body.size())));
}

void AddonStore::removeCachedManifest(PropertiesStore& cache,
                                      const QString& url)
{
    cache.remove(cacheKeyFor(url).toStdString());
}

bool AddonStore::migrateLegacyIndexedEntries(PropertiesStore& cache)
{
    bool migrated = false;
    constexpr int kLegacyHighWater = 512;
    for (int i = 0; i < kLegacyHighWater; ++i) {
        const std::string key =
            QStringLiteral("addon_%1").arg(i).toStdString();
        const auto raw = cache.getString(key);
        if (!raw) continue;
        const QJsonDocument doc =
            QJsonDocument::fromJson(QByteArray::fromStdString(*raw));
        const QString url =
            doc.object().value(QLatin1String("url")).toString();
        if (!url.isEmpty())
            saveCachedManifest(cache, url,
                               QByteArray::fromStdString(*raw));
        cache.remove(key);
        migrated = true;
    }
    return migrated;
}

} // namespace nuvio::library
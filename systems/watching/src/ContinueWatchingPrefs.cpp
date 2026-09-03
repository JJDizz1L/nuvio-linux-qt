#include "nuvio/watching/ContinueWatchingPrefs.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <filesystem>

#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::watching {

namespace {

constexpr auto kIsVisible  = "isVisible";
constexpr auto kStyle      = "style";
constexpr auto kUpNext     = "upNextFromFurthestEpisode";
constexpr auto kEpThumbs   = "use_episode_thumbnails_in_cw";
constexpr auto kUnaired    = "show_unaired_next_up";
constexpr auto kBlur       = "blur_continue_watching_next_up";
constexpr auto kDismissed  = "dismissedNextUpKeys";
constexpr auto kResume     = "showResumePromptOnLaunch";
constexpr auto kSortMode   = "sort_mode";

QString styleName(CwStyle s)
{
    switch (s) {
    case CwStyle::Wide:  return QStringLiteral("Wide");
    case CwStyle::Poster: return QStringLiteral("Poster");
    case CwStyle::Card:  return QStringLiteral("Card");
    }
    return QStringLiteral("Card");
}

bool styleFromName(const QString& name, CwStyle* out)
{
    if (name == QLatin1String("Card"))  { *out = CwStyle::Card;  return true; }
    if (name == QLatin1String("Wide"))  { *out = CwStyle::Wide;  return true; }
    if (name == QLatin1String("Poster")) { *out = CwStyle::Poster; return true; }
    return false;
}

QString sortModeName(CwSortMode m)
{
    switch (m) {
    case CwSortMode::StreamingStyle:
        return QStringLiteral("STREAMING_STYLE");
    case CwSortMode::SplitUpcoming:
        return QStringLiteral("SPLIT_UPCOMING");
    case CwSortMode::Default:
        return QStringLiteral("DEFAULT");
    }
    return QStringLiteral("DEFAULT");
}

bool sortModeFromName(const QString& name, CwSortMode* out)
{
    if (name == QLatin1String("DEFAULT")) { *out = CwSortMode::Default; return true; }
    if (name == QLatin1String("STREAMING_STYLE")) { *out = CwSortMode::StreamingStyle; return true; }
    if (name == QLatin1String("SPLIT_UPCOMING")) { *out = CwSortMode::SplitUpcoming; return true; }
    return false;
}

} // namespace

ContinueWatchingPrefs ContinueWatchingPrefsCodec::decode(const QString& json)
{
    // Compose: runCatching { json.decodeFromString<...>(payload) } — any
    // failure (malformed JSON, unknown enum NAME, wrong value type) means
    // the WHOLE payload falls back to defaults. All-or-nothing, mirrored.
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return {};
    const QJsonObject o = doc.object();

    ContinueWatchingPrefs p;
    // kotlinx semantics: fields MISSING from the JSON take their data-class
    // defaults (decode continues); fields PRESENT with an invalid value
    // (wrong type / unknown enum NAME) throw and discard the whole payload.
    const auto boolField = [&](const char* key, bool* out) {
        const auto v = o.value(QLatin1String(key));
        if (v.type() == QJsonValue::Undefined) return; // missing -> default
        if (!v.isBool()) throw false;
        *out = v.toBool();
    };
    try {
        boolField(kIsVisible, &p.isVisible);
        const auto styleV = o.value(QLatin1String(kStyle));
        if (styleV.type() != QJsonValue::Undefined &&
            (!styleV.isString() ||
             !styleFromName(styleV.toString(), &p.style)))
            throw false;
        boolField(kUpNext, &p.upNextFromFurthestEpisode);
        boolField(kEpThumbs, &p.useEpisodeThumbnails);
        boolField(kUnaired, &p.showUnairedNextUp);
        boolField(kBlur, &p.blurNextUp);
        const auto dismissed = o.value(QLatin1String(kDismissed));
        if (dismissed.type() != QJsonValue::Undefined) {
            if (!dismissed.isArray()) throw false;
            for (const auto& v : dismissed.toArray()) {
                if (!v.isString()) throw false;
                p.dismissedNextUpKeys.append(v.toString());
            }
        }
        boolField(kResume, &p.showResumePromptOnLaunch);
        const auto sortV = o.value(QLatin1String(kSortMode));
        if (sortV.type() != QJsonValue::Undefined &&
            (!sortV.isString() ||
             !sortModeFromName(sortV.toString(), &p.sortMode)))
            throw false;
    } catch (bool) {
        return {};
    }
    return p;
}

QString ContinueWatchingPrefsCodec::encode(const ContinueWatchingPrefs& p)
{
    QJsonArray dismissed;
    for (const QString& k : p.dismissedNextUpKeys) dismissed.append(k);

    QJsonObject o;
    o.insert(QLatin1String(kIsVisible), p.isVisible);
    o.insert(QLatin1String(kStyle), styleName(p.style));
    o.insert(QLatin1String(kUpNext), p.upNextFromFurthestEpisode);
    o.insert(QLatin1String(kEpThumbs), p.useEpisodeThumbnails);
    o.insert(QLatin1String(kUnaired), p.showUnairedNextUp);
    o.insert(QLatin1String(kBlur), p.blurNextUp);
    o.insert(QLatin1String(kDismissed), dismissed);
    o.insert(QLatin1String(kResume), p.showResumePromptOnLaunch);
    o.insert(QLatin1String(kSortMode), sortModeName(p.sortMode));
    return QString::fromUtf8(
        QJsonDocument(o).toJson(QJsonDocument::Compact));
}

ContinueWatchingPrefsStore::ContinueWatchingPrefsStore(const int profileId)
    : m_profileId(profileId)
{}

ContinueWatchingPrefs ContinueWatchingPrefsStore::load() const
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(
            "continue_watching_preferences"));
    const auto raw =
        store.getString("continue_watching_preferences_" +
                        std::to_string(m_profileId));
    return raw ? ContinueWatchingPrefsCodec::decode(
                     QString::fromStdString(*raw))
               : ContinueWatchingPrefs{};
}

void ContinueWatchingPrefsStore::save(const ContinueWatchingPrefs& prefs)
{
    saveRaw(ContinueWatchingPrefsCodec::encode(prefs));
}

QString ContinueWatchingPrefsStore::loadRaw() const
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(
            "continue_watching_preferences"));
    const auto raw =
        store.getString("continue_watching_preferences_" +
                        std::to_string(m_profileId));
    return raw ? QString::fromStdString(*raw) : QString();
}

void ContinueWatchingPrefsStore::saveRaw(const QString& json)
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(
            "continue_watching_preferences"));
    store.putString("continue_watching_preferences_" +
                        std::to_string(m_profileId),
                    json.toStdString());
}

} // namespace nuvio::watching
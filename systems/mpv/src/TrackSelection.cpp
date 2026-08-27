#include "nuvio/mpv/TrackSelection.h"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <functional>

namespace nuvio::mpv {
namespace tracksel {

namespace {

// VERBATIM port of LanguageCodeAliases (keep in sync with Compose).
const QHash<QString, QString>& codeAliases()
{
    static const QHash<QString, QString> m = {
        {"pt-pt", "pt"},   {"pt_br", "pt-BR"},  {"pt-br", "pt-BR"},
        {"br", "pt-BR"},   {"pob", "pt-BR"},    {"eng", "en"},
        {"spa", "es"},     {"es-419", "es-419"},{"es_419", "es-419"},
        {"es-la", "es-419"},{"es-lat", "es-419"},{"fra", "fr"},
        {"fre", "fr"},     {"deu", "de"},       {"ger", "de"},
        {"ita", "it"},     {"por", "pt"},       {"rus", "ru"},
        {"jpn", "ja"},     {"kor", "ko"},       {"zho", "zh"},
        {"chi", "zh"},     {"zht", "zh-TW"},    {"zhs", "zh-CN"},
        {"chi-tw", "zh-TW"},{"chi-cn", "zh-CN"},{"zh-tw", "zh-TW"},
        {"zh_tw", "zh-TW"},{"zh-cn", "zh-CN"},  {"zh_cn", "zh-CN"},
        {"ara", "ar"},     {"hin", "hi"},       {"nld", "nl"},
        {"dut", "nl"},     {"pol", "pl"},       {"swe", "sv"},
        {"nor", "no"},     {"dan", "da"},       {"fin", "fi"},
        {"tur", "tr"},     {"ell", "el"},       {"gre", "el"},
        {"heb", "he"},     {"tha", "th"},       {"vie", "vi"},
        {"ind", "id"},     {"msa", "ms"},       {"may", "ms"},
        {"ces", "cs"},     {"cze", "cs"},       {"hun", "hu"},
        {"ron", "ro"},     {"rum", "ro"},       {"ukr", "uk"},
        {"bul", "bg"},     {"hrv", "hr"},       {"srp", "sr"},
        {"slk", "sk"},     {"slo", "sk"},       {"slv", "sl"},
        {"cat", "ca"},     {"alb", "sq"},       {"sqi", "sq"},
        {"bos", "bs"},     {"mac", "mk"},       {"mkd", "mk"},
        {"lav", "lv"},     {"lit", "lt"},       {"est", "et"},
        {"isl", "is"},     {"ice", "is"},       {"glg", "gl"},
        {"baq", "eu"},     {"eus", "eu"},       {"wel", "cy"},
        {"cym", "cy"},     {"gle", "ga"},       {"ben", "bn"},
        {"tam", "ta"},     {"tel", "te"},       {"mal", "ml"},
        {"kan", "kn"},     {"mar", "mr"},       {"pan", "pa"},
        {"guj", "gu"},     {"urd", "ur"},       {"fas", "fa"},
        {"per", "fa"},     {"amh", "am"},       {"swa", "sw"},
        {"zul", "zu"},     {"afr", "af"},       {"mlt", "mt"},
        {"bel", "be"},     {"geo", "ka"},       {"kat", "ka"},
        {"arm", "hy"},     {"hye", "hy"},       {"aze", "az"},
        {"kaz", "kk"},     {"uzb", "uz"},       {"mon", "mn"},
        {"khm", "km"},     {"lao", "lo"},       {"mya", "my"},
        {"bur", "my"},     {"sin", "si"},       {"nep", "ne"},
        {"tgl", "tl"},     {"fil", "tl"},
    };
    return m;
}

// VERBATIM port of LanguageNameAliases (keep in sync with Compose).
const QHash<QString, QString>& nameAliases()
{
    static const QHash<QString, QString> m = {
        {"afrikaans", "af"},   {"albanian", "sq"},  {"amharic", "am"},
        {"arabic", "ar"},     {"armenian", "hy"},  {"azerbaijani", "az"},
        {"basque", "eu"},     {"belarusian", "be"},{"bengali", "bn"},
        {"bosnian", "bs"},    {"bulgarian", "bg"}, {"burmese", "my"},
        {"catalan", "ca"},    {"chinese", "zh"},   {"mandarin", "zh"},
        {"croatian", "hr"},   {"czech", "cs"},     {"danish", "da"},
        {"dutch", "nl"},      {"english", "en"},   {"estonian", "et"},
        {"filipino", "tl"},   {"finnish", "fi"},   {"french", "fr"},
        {"galician", "gl"},   {"georgian", "ka"},  {"german", "de"},
        {"greek", "el"},      {"gujarati", "gu"},  {"hebrew", "he"},
        {"hindi", "hi"},      {"hungarian", "hu"}, {"icelandic", "is"},
        {"indonesian", "id"}, {"irish", "ga"},     {"italian", "it"},
        {"japanese", "ja"},   {"kannada", "kn"},   {"kazakh", "kk"},
        {"khmer", "km"},      {"korean", "ko"},     {"lao", "lo"},
        {"latvian", "lv"},    {"lithuanian", "lt"},{"macedonian", "mk"},
        {"malay", "ms"},      {"malayalam", "ml"}, {"maltese", "mt"},
        {"marathi", "mr"},    {"mongolian", "mn"}, {"nepali", "ne"},
        {"norwegian", "no"},  {"persian", "fa"},   {"polish", "pl"},
        {"punjabi", "pa"},    {"romanian", "ro"},  {"russian", "ru"},
        {"serbian", "sr"},    {"sinhala", "si"},   {"slovak", "sk"},
        {"slovenian", "sl"},  {"swahili", "sw"},   {"swedish", "sv"},
        {"tamil", "ta"},      {"telugu", "te"},    {"thai", "th"},
        {"turkish", "tr"},    {"ukrainian", "uk"}, {"urdu", "ur"},
        {"uzbek", "uz"},      {"vietnamese", "vi"},{"welsh", "cy"},
        {"zulu", "zu"},
    };
    return m;
}

// AvailableLanguageOptions membership (settings list codes, verbatim set).
const QSet<QString>& knownOptions()
{
    static const QSet<QString> s = {
        "af","sq","am","ar","hy","az","eu","be","bn","bs","bg","my",
        "ca","zh","zh-CN","zh-TW","hr","cs","da","nl","en","et","tl",
        "fi","fr","gl","ka","de","el","gu","he","hi","hu","is",
        "id","ga","it","ja","kn","kk","km","ko","lo","lv","lt",
        "mk","ms","ml","mt","mr","mn","ne","no","pa","fa","pl",
        "pt","pt-BR","ro","ru","sr","si","sk","sl","es","es-419",
        "sw","sv","ta","te","th","tr","uk","ur","uz","vi","cy","zu",
    };
    return s;
}

QString tokenizedOf(const QString& raw)
{
    QString t = raw;
    t.replace(QLatin1Char('-'), QLatin1Char(' '))
     .replace(QLatin1Char('.'), QLatin1Char(' '))
     .replace(QLatin1Char('/'), QLatin1Char(' '));
    return t.simplified();
}

bool containsAny(const QString& hay, std::initializer_list<const char*> needles)
{
    for (const auto* n : needles)
        if (hay.contains(QString::fromLatin1(n))) return true;
    return false;
}
} // namespace

QString normalizeLanguageCode(const QString& raw)
{
    const QString trimmed = raw.trimmed().replace(QLatin1Char('_'), QLatin1Char('-')); 
    if (trimmed.isEmpty()) return {};
    const QString lowered = trimmed.toLower();
    const QString tokenized = tokenizedOf(lowered);

    if (containsAny(tokenized, {"portuguese", "portugues"})) {
        if (containsAny(tokenized, {"brazil", "brasil", "brazilian",
                                    "brasileiro", "pt br", "ptbr", "pob",
                                    "(br)"}))
            return QStringLiteral("pt-BR");
        return QStringLiteral("pt");
    }

    if (containsAny(tokenized, {"spanish", "espanol", "castellano"})) {
        if (containsAny(tokenized, {"latin", "latino", "latinoamerica",
                                    "latinoamericano", "lat am", "latam",
                                    "es 419", "es419", "(419)"}))
            return QStringLiteral("es-419");
        return QStringLiteral("es");
    }

    const auto code = codeAliases();
    if (auto it = code.constFind(lowered); it != code.end())
        return it->toLower().replace('_','-');
    const auto names = nameAliases();
    if (auto it = names.constFind(tokenized); it != names.end()) return *it;

    // Longest-name containment sweep (Compose parity: exact, prefix "x ",
    // suffix " x", or contained " x ").
    QString best;
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        if (it.key().size() <= best.size()) continue;
        const bool hit = tokenized == it.key()
            || tokenized.startsWith(it.key() + QLatin1Char(' '))
            || tokenized.endsWith(QLatin1Char(' ') + it.key())
            || tokenized.contains(QLatin1Char(' ') + it.key() + QLatin1Char(' '));
        if (hit) best = it.key();
    }
    if (!best.isEmpty()) return *names.constFind(best);

    const QString primary    = lowered.section(QLatin1Char('-'), 0, 0);
    const QString suffix     = lowered.section(QLatin1Char('-'), 1);
    const auto pa            = codeAliases().constFind(primary);
    const QString primaryAlias =
        pa != codeAliases().constEnd()
            ? pa->toLower().replace('_', '-') : primary;

    if (suffix.isEmpty())
        return primaryAlias.isEmpty() ? QString{} : primaryAlias;
    if (primaryAlias != primary && !primaryAlias.contains(QLatin1Char('-')))
        return primaryAlias + QLatin1Char('-') + suffix;
    return lowered;
}

bool languageMatchesPreference(const QString& trackLanguage,
                               const QString& targetLanguage)
{
    const QString t = normalizeLanguageCode(trackLanguage);
    if (t.isEmpty()) return false;
    const QString g = normalizeLanguageCode(targetLanguage);
    if (g.isEmpty()) return false;
    if (t.compare(g, Qt::CaseInsensitive) == 0) return true;
    return t.section(QLatin1Char('-'), 0, 0)
        .compare(g.section(QLatin1Char('-'), 0, 0), Qt::CaseInsensitive) == 0;
}

bool isKnownLanguageCode(const QString& normalizedCode)
{
    return knownOptions().contains(normalizedCode);
}

QString resolveAudioTrackLanguageTarget(const TrackInfo& track, bool* ok)
{
    if (ok) *ok = false;
    const QString direct = normalizeLanguageCode(track.lang);
    if (!direct.isEmpty() && direct != QLatin1String("und")
        && direct != QLatin1String("unknown")) {
        if (ok) *ok = true;
        return direct;
    }
    // Label/id fallback: must normalize into the settings' option list.
    for (const QString& candidate : {track.title, track.codec}) {
        const QString n = normalizeLanguageCode(candidate);
        if (!n.isEmpty() && isKnownLanguageCode(n)) {
            if (ok) *ok = true;
            return n;
        }
    }
    return {};
}

namespace {
QString prefNormalizeAudio(const QString& v, const QString& contentOriginal)
{
    const QString n = normalizeLanguageCode(v);
    if (n.isEmpty() || n == QLatin1String(kDefault)
        || n == QLatin1String(kDevice) || n == QLatin1String(kNone)
        || n == QLatin1String(kForced))
        return {};
    if (n == QLatin1String(kOriginal)) {
        const QString o = contentOriginal.trimmed().toLower();
        return o.isEmpty() ? QString{} : o;
    }
    return n;
}
QString prefNormalizeSubtitle(const QString& v)
{
    const QString n = normalizeLanguageCode(v);
    if (n.isEmpty() || n == QLatin1String(kNone)
        || n == QLatin1String(kDefault))
        return {};
    return n;
}
QStringList distinct(const QStringList& in)
{
    QStringList out;
    for (const auto& s : in)
        if (!s.isEmpty() && !out.contains(s, Qt::CaseInsensitive)) out.append(s);
    return out;
}
} // namespace

QStringList resolvePreferredAudioTargets(const LanguagePrefs& prefs,
                                         const QString& contentOriginalLang)
{
    QString primary = normalizeLanguageCode(prefs.preferredAudio);
    if (primary.isEmpty()) primary = QLatin1String(kDevice);

    if (primary == QLatin1String(kDefault))
        return distinct({prefNormalizeAudio(prefs.secondaryAudio,
                                            contentOriginalLang)});
    if (primary == QLatin1String(kDevice)) {
        QStringList chain;
        for (const QString& d : prefs.deviceLanguages)
            chain.append(prefNormalizeAudio(d, contentOriginalLang));
        chain.append(
            prefNormalizeAudio(prefs.secondaryAudio, contentOriginalLang));
        return distinct(chain);
    }
    if (primary == QLatin1String(kOriginal)) {
        const QString original = contentOriginalLang.trimmed().toLower();
        if (!original.isEmpty())
            return distinct({original, prefNormalizeAudio(
                prefs.secondaryAudio, contentOriginalLang)});
        // Unknown original -> device fallback (Compose parity).
        QStringList chain;
        for (const QString& d : prefs.deviceLanguages)
            chain.append(prefNormalizeAudio(d, contentOriginalLang));
        chain.append(
            prefNormalizeAudio(prefs.secondaryAudio, contentOriginalLang));
        return distinct(chain);
    }
    return distinct({prefNormalizeAudio(prefs.preferredAudio,
                                        contentOriginalLang),
                     prefNormalizeAudio(prefs.secondaryAudio,
                                        contentOriginalLang)});
}

QStringList resolvePreferredSubtitleTargets(const LanguagePrefs& prefs)
{
    QString primary = normalizeLanguageCode(prefs.preferredSubtitle);
    if (primary.isEmpty()) primary = QLatin1String(kNone);

    if (primary == QLatin1String(kNone))
        return distinct({prefNormalizeSubtitle(prefs.secondarySubtitle)});
    if (primary == QLatin1String(kDevice)) {
        QStringList chain;
        for (const QString& d : prefs.deviceLanguages)
            chain.append(prefNormalizeSubtitle(d));
        chain.append(prefNormalizeSubtitle(prefs.secondarySubtitle));
        return distinct(chain);
    }
    return distinct({prefNormalizeSubtitle(prefs.preferredSubtitle),
                     prefNormalizeSubtitle(prefs.secondarySubtitle)});
}

std::optional<SubtitlePlan> resolveSubtitleAutoSelectionPlan(
    const QString& selectedAudioLang, const QStringList& audioTargets,
    const QStringList& subtitleTargets, bool useForcedSubtitles)
{
    const QString audio = normalizeLanguageCode(selectedAudioLang);
    if (useForcedSubtitles && audio.isEmpty()) return std::nullopt;

    QStringList targets;
    for (const QString& t : subtitleTargets) {
        const QString n = normalizeLanguageCode(t);
        if (n.isEmpty() || n == QLatin1String(kNone)
            || n == QLatin1String(kForced) || n == QLatin1String(kDefault))
            continue;
        if (!targets.contains(n, Qt::CaseInsensitive)) targets.append(n);
    }
    const QString primaryTarget = targets.value(0);

    QString forcedTarget;
    if (useForcedSubtitles && !primaryTarget.isEmpty()
        && !audio.isEmpty()
        && languageMatchesPreference(audio, primaryTarget))
        forcedTarget = primaryTarget;
    else if (useForcedSubtitles && primaryTarget.isEmpty() && !audio.isEmpty()) {
        for (const QString& t : audioTargets)
            if (languageMatchesPreference(audio, t)) {
                forcedTarget = audio;   // Compose: normalized AUDIO language
                break;
            }
    }

    SubtitlePlan plan;
    if (!forcedTarget.isEmpty()) {
        plan.targets = {forcedTarget};
        plan.mode    = SubtitleSelectionMode::ForcedOnly;
    } else {
        plan.targets = targets;
        plan.mode    = SubtitleSelectionMode::NormalOnly;
    }
    return plan;
}

bool inferForcedSubtitleTrack(const QString& label, const QString& language,
                              const QString& trackId, bool mpvForcedFlag)
{
    if (mpvForcedFlag) return true;
    const QString n = normalizeLanguageCode(language);
    if (n == QLatin1String(kForced)) return true;
    QStringList parts{label, language, trackId};
    const QString text = parts.join(QLatin1Char(' ')).toLower();
    if (text.contains(QLatin1String("forced"))) return true;
    return text.contains(QLatin1String("songs"))
        && text.contains(QLatin1String("sign"));
}

int findPreferredAudioTrackIndex(const QVector<TrackInfo>& tracks,
                                 const QStringList& targets)
{
    if (targets.isEmpty()) return -1;
    for (const QString& target : targets) {
        for (int i = 0; i < tracks.size(); ++i) {
            bool ok     = false;
            const QString lang = resolveAudioTrackLanguageTarget(tracks[i], &ok);
            if (ok && languageMatchesPreference(lang, target)) return i;
        }
    }
    return -1;
}

int findPreferredSubtitleTrackIndex(const QVector<TrackInfo>& tracks,
                                    const QStringList& targets,
                                    SubtitleSelectionMode mode)
{
    if (targets.isEmpty()) return -1;
    for (const QString& rawTarget : targets) {
        const QString target = normalizeLanguageCode(rawTarget);
        if (target.isEmpty()) continue;
        for (int i = 0; i < tracks.size(); ++i) {
            const TrackInfo& t = tracks[i];
            const bool forced = inferForcedSubtitleTrack(
                t.title, t.lang, t.codec, t.forced);
            if (mode == SubtitleSelectionMode::ForcedOnly && !forced) continue;
            if (mode == SubtitleSelectionMode::NormalOnly && forced) continue;
            for (const QString& cand : {t.lang, t.title, t.codec})
                if (languageMatchesPreference(cand, target)) return i;
        }
    }
    return -1;
}

} // namespace tracksel
} // namespace nuvio::mpv

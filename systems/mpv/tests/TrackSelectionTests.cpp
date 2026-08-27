// Offline contract for the track-preference selection kernel (parity port
// of PlayerTrackSelection + PlayerLanguagePreferences).
#include <nuvio/mpv/TrackSelection.h>

#include <QCoreApplication>
#include <cstdio>

using namespace nuvio::mpv;
using namespace nuvio::mpv::tracksel;
static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

static TrackInfo audio(qint64 id, const char* lang, const char* title)
{
    TrackInfo t;
    t.id = id; t.kind = TrackKind::Audio;
    t.lang = lang; t.title = title;
    return t;
}
static TrackInfo sub(qint64 id, const char* lang, const char* title,
                     bool forced = false)
{
    TrackInfo t;
    t.id = id; t.kind = TrackKind::Subtitle;
    t.lang = lang; t.title = title; t.forced = forced;
    return t;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    { // normalization anchors (Compose special cases)
        CHECK(normalizeLanguageCode(QStringLiteral("POB")) == "pt-br",
              "pob alias hit lowercases (Compose behavior)");
        CHECK(normalizeLanguageCode(QStringLiteral("portuguese")) == "pt",
              "plain portuguese -> pt");
        CHECK(normalizeLanguageCode(QStringLiteral("Portuguese (Brazil)"))
                  == "pt-BR", "named brazil variant");
        CHECK(normalizeLanguageCode(QStringLiteral("Spanish (Latin America)"))
                  == "es-419", "latam spanish");
        CHECK(normalizeLanguageCode(QStringLiteral("eng")) == "en",
              "iso alias eng->en");
        CHECK(normalizeLanguageCode(QStringLiteral("Mandarin")) == "zh",
              "name alias");
        CHECK(normalizeLanguageCode(QStringLiteral("pt_BR")) == "pt-br",
              "underscore form lowercased through alias");
        CHECK(normalizeLanguageCode(QString()).isEmpty(),
              "empty stays null-like");
    }
    {
        CHECK(languageMatchesPreference(QStringLiteral("en-US"),
                                        QStringLiteral("en")),
              "primary subtag match");
        CHECK(!languageMatchesPreference(QStringLiteral("fr"),
                                         QStringLiteral("en")),
              "distinct rejected");
        CHECK(languageMatchesPreference(QStringLiteral("Brazilian Portuguese"),
                                        QStringLiteral("pt-BR")),
              "heuristic name vs region code");
    }

    {
        LanguagePrefs p;
        p.preferredAudio = QStringLiteral("device");
        p.deviceLanguages = QStringList{QStringLiteral("de-DE"),
                                        QStringLiteral("en")};
        p.secondaryAudio  = QStringLiteral("ja");
        const auto chain = resolvePreferredAudioTargets(p);
        CHECK(chain.size() == 3 && chain[1] == QLatin1String("en")
                  && chain[2] == QLatin1String("ja"),
              "device chain ordered then secondary");

        p.preferredAudio = QStringLiteral("original");
        const auto orig = resolvePreferredAudioTargets(
            p, QStringLiteral("ko"));
        CHECK(orig.size() == 2 && orig[0] == QLatin1String("ko")
                  && orig[1] == QLatin1String("ja"),
              "original wins then secondary");

        const auto fb = resolvePreferredAudioTargets(p);
        CHECK(fb.size() == 3, "unknown original -> device+secondary fallback");
    }
    { // audio finder incl label fallback for missing lang tags
        const QVector<TrackInfo> tracks{
            audio(1, "", "English"),
            audio(2, "jpn", ""),
            audio(3, "fre", "French"),
        };
        CHECK(findPreferredAudioTrackIndex(tracks,
                                           {QStringLiteral("en")}) == 0,
              "label-based english resolved via option list");
        CHECK(findPreferredAudioTrackIndex(tracks,
                                           {QStringLiteral("zz")}) < 0,
              "no match keeps container default (-1)");
    }
    { // subtitle plan: forced branch when audio matches primary sub target
        const auto plan = resolveSubtitleAutoSelectionPlan(
            QStringLiteral("en"), {QStringLiteral("en")},
            {QStringLiteral("en")}, true);
        CHECK(plan.has_value() && plan->mode
                  == SubtitleSelectionMode::ForcedOnly,
              "audio==primary target -> forced-only plan");

        const auto normal = resolveSubtitleAutoSelectionPlan(
            QStringLiteral("en"), {}, {QStringLiteral("pt-BR"),
                                       QStringLiteral("es")}, false);
        CHECK(normal.has_value()
                  && normal->mode == SubtitleSelectionMode::NormalOnly
                  && normal->targets.size() == 2,
              "non-forced plan keeps both targets in order");
    }
    { // subtitle finder: forced-only mode + signs/songs heuristic
        const QVector<TrackInfo> subs{
            sub(1, "eng", "English [SDH]"),          // 'signs' not here -> SDH? no: contains 'songs'? no. plain
            sub(2, "eng", "English forced", true),
            sub(3, "por", "Brazilian"),
        };
        CHECK(findPreferredSubtitleTrackIndex(
                  subs, {QStringLiteral("en")},
                  SubtitleSelectionMode::ForcedOnly) == 1,
              "forced-only skips non-forced english");
        CHECK(findPreferredSubtitleTrackIndex(
                  subs, {QStringLiteral("en")},
                  SubtitleSelectionMode::NormalOnly) == 0,
              "normal-only picks first matching non-forced");
        CHECK(inferForcedSubtitleTrack(QStringLiteral("Signs & Songs"),
                                       QString(), QString(), false),
              "songs+signs heuristic flags track as forced-like");
    }

    std::printf(failures ? "TRACKSEL SUITE FAILURES=%d\n"
                         : "TRACKSEL SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}

// Offline contract for the ContinueWatching preferences parity layer:
// kotlinx JSON shapes (defaults encoding, @SerialName renames, enum NAME
// strings), all-or-nothing decode semantics, profile-scoped store.
// XDG_CONFIG_HOME redirected per the test-isolation gotcha.
#include <nuvio/watching/ContinueWatchingPrefs.h>

#include <QJsonArray>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>

using nuvio::watching::ContinueWatchingPrefs;
using nuvio::watching::ContinueWatchingPrefsCodec;
using nuvio::watching::ContinueWatchingPrefsStore;
using nuvio::watching::CwSortMode;
using nuvio::watching::CwStyle;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME",
            QDir(sandbox.path()).filePath("cfg").toUtf8());
    QDir().mkpath(QString::fromUtf8(qgetenv("XDG_CONFIG_HOME")));

    { // T1: defaults encode — every key present (encodeDefaults), exact values
        const QString json = ContinueWatchingPrefsCodec::encode({});
        const QJsonObject o =
            QJsonDocument::fromJson(json.toUtf8()).object();
        CHECK(o.value(QLatin1String("isVisible")).toBool(), "isVisible");
        CHECK(o.value(QLatin1String("style")).toString() == "Card",
              "style default name");
        CHECK(o.value(QLatin1String("upNextFromFurthestEpisode")).toBool(),
              "upNext default");
        CHECK(o.value(QLatin1String("use_episode_thumbnails_in_cw")).toBool(),
              "serialname ep thumbs present");
        CHECK(o.value(QLatin1String("show_unaired_next_up")).toBool(),
              "serialname unaired present");
        CHECK(!o.value(QLatin1String("blur_continue_watching_next_up"))
                   .toBool(),
              "serialname blur default false");
        CHECK(o.value(QLatin1String("dismissedNextUpKeys")).toArray().isEmpty(),
              "dismissed default empty array");
        CHECK(o.value(QLatin1String("showResumePromptOnLaunch")).toBool(),
              "resume prompt default");
        CHECK(o.value(QLatin1String("sort_mode")).toString() == "DEFAULT",
              "sort_mode default name");
        CHECK(o.size() == 9, "encodeDefaults writes all 9 keys");
    }

    { // T2: Compose-shaped decode (mixed serial/plain keys, enum names)
        const auto p = ContinueWatchingPrefsCodec::decode(QStringLiteral(
            R"({"isVisible":false,"style":"Wide",)"
            R"("upNextFromFurthestEpisode":false,)"
            R"("use_episode_thumbnails_in_cw":false,)"
            R"("show_unaired_next_up":false,)"
            R"("blur_continue_watching_next_up":true,)"
            R"("dismissedNextUpKeys":["tt:a:s:e","kitsu:1_s1e2"],)"
            R"("showResumePromptOnLaunch":false,"sort_mode":"SPLIT_UPCOMING"})"));
        CHECK(!p.isVisible, "decode isVisible");
        CHECK(p.style == CwStyle::Wide, "decode style");
        CHECK(!p.upNextFromFurthestEpisode, "decode upNext");
        CHECK(!p.useEpisodeThumbnails, "decode ep thumbs");
        CHECK(!p.showUnairedNextUp, "decode unaired");
        CHECK(p.blurNextUp, "decode blur");
        CHECK(p.dismissedNextUpKeys.size() == 2, "decode dismissed");
        CHECK(!p.showResumePromptOnLaunch, "decode resume prompt");
        CHECK(p.sortMode == CwSortMode::SplitUpcoming, "decode sort mode");
    }

    { // T3: all-or-nothing failure semantics (kotlinx throw -> defaults)
        const ContinueWatchingPrefs d;
        CHECK(ContinueWatchingPrefsCodec::decode(QStringLiteral("garbage")) ==
                  d,
              "garbage -> defaults");
        CHECK(ContinueWatchingPrefsCodec::decode(QStringLiteral("[1,2]")) == d,
              "non-object -> defaults");
        CHECK(ContinueWatchingPrefsCodec::decode(QStringLiteral(
                  R"({"style":"Bogus"})")) == d,
              "unknown enum NAME -> whole payload defaults");
        CHECK(ContinueWatchingPrefsCodec::decode(QStringLiteral(
                  R"({"isVisible":"yes"})")) == d,
              "wrong value type -> whole payload defaults");
        // Unknown KEYS are fine (ignoreUnknownKeys); MISSING keys take
        // data-class defaults (kotlinx default-value semantics)
        const auto p = ContinueWatchingPrefsCodec::decode(QStringLiteral(
            R"({"isVisible":false,"unknown_future_key":123})"));
        CHECK(!p.isVisible && p.style == CwStyle::Card,
              "unknown keys ignored, known applied");
        const auto q = ContinueWatchingPrefsCodec::decode(QStringLiteral(
            R"({"isVisible":false})"));
        CHECK(!q.isVisible && q.style == CwStyle::Card &&
                  q.sortMode == CwSortMode::Default &&
                  q.dismissedNextUpKeys.isEmpty(),
              "missing keys take defaults, decode continues");
    }
    { // T4: round-trip non-default values
        ContinueWatchingPrefs p;
        p.isVisible = false;
        p.style = CwStyle::Poster;
        p.upNextFromFurthestEpisode = false;
        p.useEpisodeThumbnails = false;
        p.showUnairedNextUp = false;
        p.blurNextUp = true;
        p.dismissedNextUpKeys = {"tt123_s1e4", "kitsu:9_s2e1"};
        p.showResumePromptOnLaunch = false;
        p.sortMode = CwSortMode::StreamingStyle;
        const auto back = ContinueWatchingPrefsCodec::decode(
            ContinueWatchingPrefsCodec::encode(p));
        CHECK(back.isVisible == p.isVisible && back.style == p.style &&
                  back.upNextFromFurthestEpisode ==
                      p.upNextFromFurthestEpisode &&
                  back.useEpisodeThumbnails == p.useEpisodeThumbnails &&
                  back.showUnairedNextUp == p.showUnairedNextUp &&
                  back.blurNextUp == p.blurNextUp &&
                  back.dismissedNextUpKeys == p.dismissedNextUpKeys &&
                  back.showResumePromptOnLaunch == p.showResumePromptOnLaunch &&
                  back.sortMode == p.sortMode,
              "round-trip preserves all fields");
    }

    { // T5: profile-scoped store round-trip + cross-profile isolation
        ContinueWatchingPrefsStore a(1);
        ContinueWatchingPrefs p;
        p.isVisible = false;
        p.sortMode = CwSortMode::StreamingStyle;
        p.dismissedNextUpKeys = {"kitsu:48899_s1e1"};
        a.save(p);

        const auto ra = ContinueWatchingPrefsStore(1).load();
        CHECK(!ra.isVisible && ra.sortMode == CwSortMode::StreamingStyle &&
                  ra.dismissedNextUpKeys.size() == 1,
              "store round-trip profile 1");
        const auto rb = ContinueWatchingPrefsStore(2).load();
        CHECK(rb.isVisible && rb.sortMode == CwSortMode::Default &&
                  rb.dismissedNextUpKeys.isEmpty(),
              "profile 2 unaffected (defaults)");
    }
}
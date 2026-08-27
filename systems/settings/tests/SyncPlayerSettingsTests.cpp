// Offline contract: player-settings sync feature payload (blob v3 leg).
// ISOLATION: XDG_CONFIG_HOME redirected to temp (real profile is live data).
#include <nuvio/settings/SyncPlayerSettings.h>

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <nuvio/settings/PropertiesStore.h>

#include <cstdio>

using nuvio::settings::PlayerSettingsSync;
using Store = nuvio::settings::PropertiesStore;

namespace {
QJsonObject SyncEnvelopeString(const QString& v)
{
    return QJsonObject{{QLatin1String("type"), QLatin1String("string")},
                       {QLatin1String("value"), v}};
}
} // namespace

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

    const auto path = Store::defaultPath("player_settings");

    { // export: present keys only, kotlinx-shaped envelopes
        Store seed(path);
        seed.putString("preferred_audio_language_1", "ja");
        seed.putBoolean("subtitle_use_forced_subtitles_1", false);
        seed.putInt("decoder_priority_1", 2);
        seed.persist();

        Store view(path);   // snapshot AFTER seeding
        const auto out = PlayerSettingsSync::exportSyncPayload(view);
        const QString json =
            QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
        CHECK(json.contains(R"("preferred_audio_language_1":{"type":"string","value":"ja"})"),
              "audio string envelope");
        CHECK(json.contains(R"("subtitle_use_forced_subtitles_1":{"type":"boolean","value":false})"),
              "forced-subs boolean envelope");
        CHECK(json.contains(R"("decoder_priority_1":{"type":"int","value":2})"),
              "decoder int envelope");
        CHECK(!json.contains("preferred_subtitle_language_1"),
              "absent keys omitted from export");
    }

    { // apply: field-wise merge, invalid entries untouched
        {
            Store baseline(path);
            baseline.putString("preferred_subtitle_language_1", "en");
            baseline.putString("stream_cache_size_1", "MB_256");
            baseline.persist();
        }

        QJsonObject remote;
        remote.insert(QStringLiteral("preferred_audio_language_1"),
                      SyncEnvelopeString(QStringLiteral("de")));
        remote.insert(QStringLiteral("stream_cache_size_1"),
                      SyncEnvelopeString(QStringLiteral("GB_1")));
        remote.insert(QStringLiteral("subtitle_use_forced_subtitles_1"),
                      QJsonObject{{QLatin1String("type"), QLatin1String("boolean")},
                                  {QLatin1String("value"), true}});
        // Invalid entry: fractional int under int tag must be ignored.
        remote.insert(QStringLiteral("decoder_priority_1"),
                      QJsonObject{{QLatin1String("type"), QLatin1String("int")},
                                  {QLatin1String("value"), 0.5}});
        // Unknown future key: ignored entirely.
        remote.insert(QStringLiteral("some_future_setting_1"),
                      QJsonObject{{QLatin1String("type"), QLatin1String("string")},
                                  {QLatin1String("value"), QStringLiteral("zz")}});

        Store target(path);
        PlayerSettingsSync::applyRemotePayload(target, remote);

        Store after(path);
        CHECK(after.getString("preferred_audio_language_1").value_or("") == "de",
              "audio applied");
        CHECK(after.getString("stream_cache_size_1").value_or("") == "GB_1",
              "cache enum applied");
        CHECK(after.getBoolean("subtitle_use_forced_subtitles_1")
                       .value_or(true) == true,
              "forced subs applied");
        CHECK(after.getInt("decoder_priority_1").value_or(-1) == 2,
              "invalid int left prior value untouched");
        CHECK(after.getString("preferred_subtitle_language_1").value_or("") == "en",
              "absent remote key left local value");
    }

    std::printf(failures ? "SYNC-PLAYER SUITE FAILURES=%d\n"
                         : "SYNC-PLAYER SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
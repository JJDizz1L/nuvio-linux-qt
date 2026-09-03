// SyncBlobFeatures contract (P1b): feature-name constants, push-blob
// assembly, passthrough merge/load rules. ISOLATION: XDG_CONFIG_HOME
// redirected to temp (sync_blob_passthrough is profile data).
#include <nuvio/settings/SyncBlobFeatures.h>

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cstdio>

using nuvio::settings::BlobPassthroughStore;
using nuvio::settings::buildPushBlob;
namespace BlobFeature = nuvio::settings::BlobFeature;

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

    { // push assembly: version 3, player always present, passthrough copied
        QJsonObject player{
            {QStringLiteral("decoder_priority_1"),
             QJsonObject{{QLatin1String("type"), QLatin1String("int")},
                         {QLatin1String("value"), 2}}}};
        QJsonObject passthrough{
            {QLatin1String(BlobFeature::kTheme),
             QJsonObject{{QStringLiteral("x"),
                          QJsonObject{{QLatin1String("type"),
                                       QLatin1String("string")},
                                      {QLatin1String("value"),
                                       QStringLiteral("y")}}}}},
            {QLatin1String(BlobFeature::kPosterCardStyle),
             QStringLiteral("raw-payload")},
            // A stale player entry inside the cache loses to the fresh one.
            {QLatin1String(BlobFeature::kPlayer), QJsonObject{}}};
        const QJsonObject blob = buildPushBlob(player, passthrough);
        CHECK(blob.value(QStringLiteral("version")).toInt() == 3,
              "blob version 3");
        const QJsonObject features =
            blob.value(QStringLiteral("features")).toObject();
        CHECK(features.value(QStringLiteral("player_settings")).toObject()
                  .contains(QStringLiteral("decoder_priority_1")),
              "fresh player fragment present");
        CHECK(features.value(QStringLiteral("theme_settings")).toObject()
                  .contains(QStringLiteral("x")),
              "passthrough object copied");
        CHECK(features.value(QStringLiteral("poster_card_style_settings_"
                                             "payload"))
                      .toString()
                  == "raw-payload",
              "passthrough string copied");
    }

    { // passthrough: merge caches objects/strings/bools, skips player + CW
      // + nulls; partial pulls never evict; corrupt entries never forward.
        BlobPassthroughStore store;
        CHECK(store.loadAll().isEmpty(), "cold cache loads empty");

        QJsonObject pull;
        pull.insert(QLatin1String(BlobFeature::kTheme),
                    QJsonObject{{QStringLiteral("a"),
                                 QJsonObject{{QLatin1String("type"),
                                              QLatin1String("string")},
                                             {QLatin1String("value"),
                                              QStringLiteral("b")}}}});
        pull.insert(QLatin1String(BlobFeature::kMetaScreen),
                    QStringLiteral("meta-payload"));
        pull.insert(QLatin1String(BlobFeature::kNotifications),
                    QJsonObject{{QStringLiteral("episode_release_alerts_"
                                                "enabled"),
                                 QJsonObject{{QLatin1String("type"),
                                              QLatin1String("boolean")},
                                             {QLatin1String("value"),
                                              true}}}});
        pull.insert(QLatin1String(BlobFeature::kPlayer), QJsonObject{});
        pull.insert(QLatin1String(BlobFeature::kContinueWatching),
                    QStringLiteral("cw-goes-to-its-own-store"));
        pull.insert(QStringLiteral("future_feature"), QJsonValue());
        store.mergeFromPull(pull);

        const QJsonObject cached = store.loadAll();
        CHECK(cached.contains(QLatin1String(BlobFeature::kTheme)),
              "theme cached");
        CHECK(cached.value(QLatin1String(BlobFeature::kMetaScreen))
                      .toString()
                  == "meta-payload",
              "string payload cached");
        CHECK(cached.value(QLatin1String(BlobFeature::kNotifications))
                          .toObject()
                          .value(QStringLiteral(
                              "episode_release_alerts_enabled"))
                          .toObject()
                          .value(QStringLiteral("value"))
                          .toBool()
                      == true,
              "notifications map cached");
        CHECK(!cached.contains(QLatin1String(BlobFeature::kPlayer)),
              "player fragment never cached");
        CHECK(!cached.contains(
                  QLatin1String(BlobFeature::kContinueWatching)),
              "CW payload never cached (own store)");
        CHECK(!cached.contains(QStringLiteral("future_feature")),
              "null values never cached");

        // Partial second pull: absent features keep their copies.
        QJsonObject partial;
        partial.insert(QLatin1String(BlobFeature::kMdbList),
                       QJsonObject{});
        store.mergeFromPull(partial);
        const QJsonObject kept = store.loadAll();
        CHECK(kept.contains(QLatin1String(BlobFeature::kTheme)),
              "partial pull does not evict");
        CHECK(kept.contains(QLatin1String(BlobFeature::kMdbList)),
              "empty object is a receivable value, not a wipe vector");

        // Fresh view sees the same rows (persisted, cross-instance).
        BlobPassthroughStore view;
        CHECK(view.loadAll().contains(
                  QLatin1String(BlobFeature::kTheme)),
              "passthrough persists across instances");
    }

    std::printf(failures ? "SYNC-BLOB SUITE FAILURES=%d\n"
                         : "SYNC-BLOB SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}

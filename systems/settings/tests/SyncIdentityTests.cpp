// Offline contract: SyncIdentity parity with Compose SyncClientIdentity.
// ISOLATION: XDG_CONFIG_HOME redirected to temp (real profile is live data).
#include <nuvio/settings/SyncIdentity.h>

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <nuvio/settings/PropertiesStore.h>

#include <cstdio>

using nuvio::settings::PropertiesStore;
using nuvio::settings::SyncIdentity;

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

    { // validity contract table
        CHECK(SyncIdentity::isValidClientId(
                  "nuvio-mobile-abdub0cos7cn2jatg385x49a24razato"),
              "real-world desktop id valid");
        CHECK(SyncIdentity::isValidClientId("abcdefghijklmnop"),  // 16
              "16-char floor accepted");
        CHECK(SyncIdentity::isValidClientId(QString(96, 'a')),
              "96-char ceiling accepted");
        CHECK(!SyncIdentity::isValidClientId("abcdefghijklmno"),  // 15
              "15 chars rejected");
        CHECK(!SyncIdentity::isValidClientId(QString(97, 'a')),
              "97 chars rejected");
        CHECK(!SyncIdentity::isValidClientId("has spaces here"),
              "spaces rejected");
        CHECK(!SyncIdentity::isValidClientId("bad/slash-id-0000000"),
              "slash rejected");
        CHECK(!SyncIdentity::isValidClientId(""),
              "empty rejected");
    }

    const auto storePath = PropertiesStore::defaultPath("sync_client_identity");

    { // generation + persistence round-trip
        PropertiesStore store(storePath);
        const QString id = SyncIdentity::currentClientId(store);

        static const QRegularExpression shape(
            QStringLiteral("^nuvio-mobile-[a-z0-9]{32}$"));
        CHECK(shape.match(id).hasMatch(),
              "generated id matches upstream format");
        CHECK(id == SyncIdentity::currentClientId(store),
              "stable within same instance");

        // Fresh view (snapshot-at-construction gotcha) sees it from disk.
        PropertiesStore fresh(storePath);
        const auto raw = fresh.getString("client_instance_id");
        CHECK(raw.has_value() && QString::fromStdString(*raw) == id,
              "generated id persisted to the shared-format store");
    }

    { // adopting a pre-existing VALID Compose-written id unchanged
        PropertiesStore writer(storePath);
        writer.putString("client_instance_id",
                         "nuvio-mobile-abdub0cos7cn2jatg385x49a24razato");

        PropertiesStore reader(storePath);   // snapshot AFTER seeding
        CHECK(SyncIdentity::currentClientId(reader) ==
                  "nuvio-mobile-abdub0cos7cn2jatg385x49a24razato",
              "valid stored id adopted verbatim");
    }

    { // invalid stored ids are replaced, not trusted
        {
            PropertiesStore w(storePath);
            w.putString("client_instance_id", "not!a@valid+id");
        }
        PropertiesStore r(storePath);
        const QString id = SyncIdentity::currentClientId(r);
        static const QRegularExpression shape(
            QStringLiteral("^nuvio-mobile-[a-z0-9]{32}$"));
        CHECK(shape.match(id).hasMatch(), "invalid stored id regenerated");

        PropertiesStore after(storePath);
        CHECK(after.getString("client_instance_id")
                      .value_or("") == id.toStdString(),
              "replacement persisted");
    }

    { // reset clears; next call generates again
        PropertiesStore store(storePath);
        SyncIdentity::resetClientId(store);
        CHECK(!store.getString("client_instance_id").has_value(),
              "reset removed key");

        const QString regen = SyncIdentity::currentClientId(store);
        static const QRegularExpression shape(
            QStringLiteral("^nuvio-mobile-[a-z0-9]{32}$"));
        CHECK(shape.match(regen).hasMatch(), "regenerated after reset");
    }

    std::printf(failures ? "SYNC-ID SUITE FAILURES=%d\n"
                         : "SYNC-ID SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
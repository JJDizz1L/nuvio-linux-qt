// Offline contract: manifest validation + P4 blob-parity persistence.
// ISOLATION: XDG_CONFIG_HOME redirected to temp (real profile is live data).
//
// Coverage:
//  - AddonStore::normalizeManifestUrl vs Compose normalizeManifestUrl rules
//  - truth-store JSON byte shapes (kotlinx List<String> / Map<String,Boolean>)
//  - cross-build interop: hand-written Compose-shaped values decode correctly
//  - legacy qt-addons addon_<i> migration -> sha256-keyed cache
//  - placeholder rows for uncached URLs + enabled default-true semantics
#include <nuvio/library/AddonStore.h>
#include <nuvio/library/AddonRegistry.h>

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <nuvio/settings/PropertiesStore.h>

#include <cstdio>

using nuvio::library::AddonRegistry;
using nuvio::library::AddonStore;
using Store = nuvio::settings::PropertiesStore;

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

    { // URL normalization - Compose contract table
        using F = QString (*)(const QString&);
        F n = &AddonStore::normalizeManifestUrl;
        CHECK(n("v3-cinemeta.strem.io") ==
                  "https://v3-cinemeta.strem.io/manifest.json",
              "bare host gets scheme + suffix");
        CHECK(n("https://x.strem.io/") ==
                  "https://x.strem.io/manifest.json",
              "trailing slash dropped");
        CHECK(n("https://x.strem.io/manifest.json") ==
                  "https://x.strem.io/manifest.json",
              "existing suffix untouched");
        CHECK(n("stremio://x.strem.io") ==
                  "https://x.strem.io/manifest.json",
              "stremio:// mapped to https");
        CHECK(n("http://x.strem.io/sub/path?a=1") ==
                  "http://x.strem.io/sub/path/manifest.json?a=1",
              "query preserved after suffix insertion");
        CHECK(n("https://x.strem.io/a b") ==
                  "https://x.strem.io/a%20b/manifest.json",
              "unsafe chars percent-encoded");
        CHECK(n("  https://x.strem.io/m.json#frag ") ==
                  "https://x.strem.io/m.json/manifest.json",
              "fragment stripped, trimmed");
        CHECK(n("").isEmpty(), "empty input rejected");
    }

    { // truth store: exact kotlinx JSON byte shapes + cross-read interop
        Store truth(Store::defaultPath("addons"));

        // Hand-write what Compose would write, then read it back.
        truth.putString("installed_addon_urls_1",
                        R"(["https://a.example/manifest.json",)"
                        R"("https://b.example/manifest.json"])");
        truth.putString("addon_enabled_states_1",
                        R"({"https://b.example/manifest.json":false})");

        const auto urls = AddonStore::loadInstalledUrls(truth);
        CHECK(urls.size() == 2, "compose urls decoded");
        CHECK(urls.first() == "https://a.example/manifest.json",
              "compose url preserved");

        const auto states = AddonStore::loadEnabledStates(truth);
        CHECK(states.size() == 1, "only explicit states present");
        CHECK(!states.value("https://b.example/manifest.json", true),
              "explicit false survives");

        // Qt write-back must produce the same JSON shape.
        QStringList next{QStringLiteral("https://c.example/manifest.json"),
                         QStringLiteral("https://d example/manifest.json")};
        AddonStore::saveInstalledUrls(truth, next);
        const auto rawUrls = truth.getString("installed_addon_urls_1");
        CHECK(rawUrls.has_value() &&
                  *rawUrls ==
                      R"(["https://c.example/manifest.json","https://d example/manifest.json"])",
              "url list byte shape matches kotlinx");

        AddonStore::EnabledMap en;
        en.insert(QStringLiteral("https://c.example/manifest.json"), false);
        AddonStore::saveEnabledStates(truth, en);
        const auto rawEn = truth.getString("addon_enabled_states_1");
        CHECK(rawEn.has_value() &&
                  *rawEn == R"({"https://c.example/manifest.json":false})",
              "enabled map byte shape matches kotlinx");
    }

    { // cache store: hashed keys + legacy migration
        Store cache(Store::defaultPath("qt-addons"));
        cache.putString(
            "addon_0",
            R"({"url":"https://legacy.strem.io/manifest.json","id":"legacy","name":"Legacy","types":["movie"]})");

        CHECK(AddonStore::migrateLegacyIndexedEntries(cache),
              "migration reports work done");
        const QByteArray body = AddonStore::loadCachedManifest(
            cache, "https://legacy.strem.io/manifest.json");
        CHECK(body.contains("\"id\":\"legacy\""), "body moved to hash key");
        CHECK(!cache.getString("addon_0").has_value(),
              "legacy key removed");

        AddonStore::removeCachedManifest(
            cache, "https://legacy.strem.io/manifest.json");
        CHECK(AddonStore::loadCachedManifest(
                  cache,
                  "https://legacy.strem.io/manifest.json").isEmpty(),
              "cache removal works");
    }

    { // registry offline behavior over the parity stores
        Store clear(Store::defaultPath("addons"));
        clear.putString("installed_addon_urls_1",
                        R"(["https://pending.example/manifest.json"])");
        clear.putString("addon_enabled_states_1",
                        R"({"https://pending.example/manifest.json":true})");
        clear.persist();

        AddonRegistry reg;
        reg.load();
        CHECK(reg.addons().size() == 1, "truth url became a row");
        if (reg.addons().size() == 1) {
            const auto row = reg.addons().first().toMap();
            CHECK(row.value("url").toString() ==
                      "https://pending.example/manifest.json",
                  "row url from truth store");
            CHECK(row.value("id").toString().isEmpty(),
                  "uncached manifest = placeholder id");
            CHECK(row.value("enabled") == true,
                  "explicit enabled surfaces");
            CHECK(row.contains("types"), "types field always present");
        }

        int removedFired = 0;
        QObject::connect(&reg, &AddonRegistry::removed,
                         [&](QString) { ++removedFired; });
        reg.remove("");                            // silent no-op
        CHECK(removedFired == 0, "empty id remove stays quiet");

        reg.setEnabled(-1, false);                 // bounds no-op
        CHECK(reg.addons().size() >= 1, "bad index ignored");
    }

    { // parseManifest rejection paths (unchanged contract)
        CHECK(AddonRegistry::parseManifest(
                  "u", R"({"name":"NoId"})").isEmpty(),
              "missing id rejected");
        CHECK(AddonRegistry::parseManifest(
                  "u", R"({"id":"x"})").isEmpty(),
              "missing name rejected");
        CHECK(AddonRegistry::parseManifest("u", "{not json").isEmpty(),
              "garbage rejected");
    }

    std::printf(failures ? "ADDONS SUITE FAILURES=%d\n"
                         : "ADDONS SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
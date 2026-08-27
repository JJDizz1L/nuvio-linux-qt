// Offline contract: SearchHistory parity with Compose SearchHistoryRepository.
// ISOLATION: XDG_CONFIG_HOME redirected to temp (real profile is live data).
#include <nuvio/settings/SearchHistory.h>

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <nuvio/settings/PropertiesStore.h>

#include <cstdio>

using nuvio::settings::PropertiesStore;
using nuvio::settings::SearchHistory;

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

    const auto path = PropertiesStore::defaultPath("search_history");

    { // record: move-to-front dedupe + min length
        SearchHistory h;
        h.record("dune");
        h.record(" blade runner ");
        h.record("dune");                     // re-record moves to front
        h.record("x");                        // too short - ignored

        const QVariantList rec = h.recent();
        CHECK(rec.size() == 2, "two entries after dedupe");
        CHECK(rec.value(0).toString() == "dune",
              "re-record moved dune to front");
        CHECK(rec.value(1).toString() == "blade runner",
              "blade runner demoted");

        // Byte shape matches kotlinx List<String>.
        PropertiesStore raw(path);
        const auto payload = raw.getString("search_history_1");
        CHECK(payload.has_value() &&
                  *payload == R"(["dune","blade runner"])",
              "kotlinx byte shape");
    }

    { // cap at 10 + persistence across instances
        for (int i = 0; i < 14; ++i)
            SearchHistory().record(QStringLiteral("q%1").arg(i));
        SearchHistory later;
        const QVariantList rec = later.recent();
        CHECK(rec.size() == 10, "capped at 10");
        CHECK(rec.value(0).toString() == "q13", "newest kept");

        later.remove("q13");
        later.remove("nonexistent");          // silent no-op
        CHECK(!later.recent().contains("q13"), "removed by value");
        CHECK(later.recent().size() == 9, "nine remain");
    }

    { // cross-build interop: hand-written Compose payload decodes
        PropertiesStore seed(path);
        seed.putString("search_history_1",
                       R"(["interstellar","arrival"])");
        SearchHistory fresh;
        const QVariantList rec = fresh.recent();
        CHECK(rec.size() == 2 && rec.first() == "interstellar",
              "compose-written history decoded order-preserving");
    }

    { // garbage / absent payloads degrade to empty
        {
            PropertiesStore w(path);
            w.putString("search_history_1", "{not json");
        }
        SearchHistory g;
        CHECK(g.recent().isEmpty(), "garbage degrades empty");
        g.clear();
        CHECK(!PropertiesStore(path)
                    .getString("search_history_1")
                    .has_value(),
              "clear removes key");
    }

    std::printf(failures ? "SEARCH-HISTORY SUITE FAILURES=%d\n"
                         : "SEARCH-HISTORY SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
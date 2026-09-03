// Home catalog settings + shelf helpers contract. ISOLATION: XDG sandbox.
#include <nuvio/library/HomeCatalogSettings.h>
#include <nuvio/library/HomeShelves.h>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cstdio>

using nuvio::library::HomeCatalogDefinition;
using nuvio::library::HomeCatalogPayload;
using nuvio::library::HomeCatalogSettingsCodec;
using nuvio::library::HomeCatalogSettingsStore;
using nuvio::library::HomeShelfPref;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {
HomeCatalogDefinition def(const QString& key, const QString& title,
                          const QString& addon)
{
    HomeCatalogDefinition d;
    d.key = key;
    d.defaultTitle = title;
    d.addonName = addon;
    return d;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME",
            QDir(sandbox.path()).filePath("cfg").toUtf8());
    QDir().mkpath(QString::fromUtf8(qgetenv("XDG_CONFIG_HOME")));

    { // codec: defaults, round-trip, garbage tolerance
        const auto fresh =
            HomeCatalogSettingsCodec::decode(QStringLiteral("not json"));
        CHECK(fresh.heroEnabled && fresh.showCatalogType &&
                  !fresh.hideUnreleasedContent && fresh.items.isEmpty(),
              "garbage decodes to Compose defaults");

        HomeCatalogPayload p;
        p.hideUnreleasedContent = true;
        HomeShelfPref row;
        row.key = "k1";
        row.customTitle = "Mine";
        row.enabled = false;
        row.order = 3;
        p.items.append(row);
        const QString enc = HomeCatalogSettingsCodec::encode(p);
        CHECK(enc.contains("\"heroEnabled\":true") &&
                  enc.contains("\"hideUnreleasedContent\":true"),
              "encodeDefaults shape");
        const auto back = HomeCatalogSettingsCodec::decode(enc);
        CHECK(!back.items.isEmpty() && back.items[0].key == "k1" &&
                  back.items[0].customTitle == "Mine" &&
                  !back.items[0].enabled && back.items[0].order == 3 &&
                  back.hideUnreleasedContent,
              "round-trip preserves rows and flags");

        // Live-shape cross-read (fork-written payload fragment).
        const auto live = HomeCatalogSettingsCodec::decode(
            QStringLiteral("{\"heroEnabled\":true,\"showCatalogType\":true,"
                           "\"hideUnreleasedContent\":true,\"items\":["
                           "{\"key\":\"a:b:c\",\"customTitle\":\"\","
                           "\"enabled\":false,\"heroSourceEnabled\":true,"
                           "\"order\":0}]}"));
        CHECK(live.hideUnreleasedContent && live.items.size() == 1 &&
                  !live.items[0].enabled,
              "live payload decodes");
        CHECK(live.items[0].displayTitle(true) == "" &&
                  live.items[0].displayTitle(false) == "",
              "empty default title degrades to empty (no crash)");
    }

    { // displayTitle: custom wins, then type suffix rules
        HomeShelfPref p;
        p.defaultTitle = "Top Movies";
        CHECK(p.displayTitle(true) == "Top Movies", "typed title shown");
        CHECK(p.displayTitle(false) == "Top", "suffix stripped");
        p.customTitle = "Mine";
        CHECK(p.displayTitle(false) == "Mine", "custom wins");
    }

    { // reconcile: new keys append with defaults, flags survive, order kept
        HomeCatalogSettingsStore store(1);
        const auto rows = store.reconcile(
            {def("k1", "Top Movies", "Cine"),
             def("k2", "Top Series", "Cine")});
        CHECK(rows.size() == 2 && rows[0].key == "k1" &&
                  rows[0].defaultTitle == "Top Movies" &&
                  rows[0].enabled && rows[0].heroSourceEnabled,
              "fresh reconcile adopts definitions with defaults");

        HomeCatalogPayload edit = store.load();
        for (HomeShelfPref& r : edit.items) {
            if (r.key == "k1") {
                r.enabled = false;
                r.customTitle = "Renamed";
            }
        }
        store.save(edit);

        const auto rows2 = store.reconcile(
            {def("k1", "Top Movies!", "Cine"),
             def("k3", "New Rail", "Other")});
        CHECK(rows2.size() == 2, "vanished k2 drops, k3 appends");
        const HomeShelfPref first = rows2[0].key == "k1" ? rows2[0]
                                                         : rows2[1];
        CHECK(!first.enabled && first.customTitle == "Renamed" &&
                  first.defaultTitle == "Top Movies!",
              "user flags survive, live titles refresh");
        CHECK(rows2.last().key == "k3", "new key appends last");
    }

    { // release filter (ISO lexicographic) + hero pick
        using nuvio::library::applyReleaseFilter;
        using nuvio::library::pickHeroItems;
        const QVariantList items{
            QVariantMap{{"id", "a"}, {"released", "2030-01-01"}},
            QVariantMap{{"id", "b"}, {"released", "2020-05-05"}},
            QVariantMap{{"id", "c"}, {"released", ""}},
            QVariantMap{{"id", "d"}},
        };
        CHECK(applyReleaseFilter(items, false, "2026-09-03").size() == 4,
              "filter off passes everything");
        const auto kept =
            applyReleaseFilter(items, true, "2026-09-03");
        CHECK(kept.size() == 3 &&
                  kept[0].toMap().value("id") == "b",
              "future row dropped, dateless kept");

        const QVariantList sections{
            QVariantMap{{"key", "s1"},
                        {"items", QVariantList{QVariantMap{{"id", "x"}}}}},
            QVariantMap{{"key", "s2"}, {"items", QVariantList{}}},
            QVariantMap{{"key", "s3"},
                        {"items", QVariantList{QVariantMap{{"id", "y"}}}}},
        };
        const auto hero = pickHeroItems(sections);
        CHECK(hero.size() == 2 &&
                  hero[0].toMap().value("id") == "x" &&
                  hero[1].toMap().value("id") == "y",
              "hero takes first items, skips empties, caps at 2");
    }

    std::printf(failures ? "HOME SUITE FAILURES=%d\n"
                         : "HOME SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}

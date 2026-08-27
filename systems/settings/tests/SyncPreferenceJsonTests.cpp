#include "nuvio/settings/SyncPreferenceJson.h"

#include <QJsonDocument>
#include <QSet>

#include <algorithm>
#include <cstdio>

using nuvio::settings::SyncPreferenceJson;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {
[[nodiscard]] QString serialized(const QJsonObject& o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(
        QJsonDocument::Compact));
}
} // namespace

int main()
{
    { // emission byte shapes vs kotlinx buildJsonObject output
        CHECK(serialized(SyncPreferenceJson::encodeString("en")) ==
                  R"({"type":"string","value":"en"})",
              "string shape");
        CHECK(serialized(SyncPreferenceJson::encodeBoolean(false)) ==
                  R"({"type":"boolean","value":false})",
              "boolean shape");
        CHECK(serialized(SyncPreferenceJson::encodeInt(2)) ==
                  R"({"type":"int","value":2})",
              "int shape");
        CHECK(serialized(SyncPreferenceJson::encodeFloat(1.5f)) ==
                  R"({"type":"float","value":1.5})",
              "float shape");

        QSet<QString> set{QStringLiteral("z"), QStringLiteral("a"),
                          QStringLiteral("m")};
        CHECK(serialized(SyncPreferenceJson::encodeStringSet(set)) ==
                  R"({"type":"string_set","value":["a","m","z"]})",
              "string_set sorted shape");
    }

    { // decode acceptance mirroring Kotlin *OrNull leniency
        QJsonObject obj;
        obj.insert(QStringLiteral("s"),
                   SyncPreferenceJson::encodeString(QStringLiteral("x")));
        obj.insert(QStringLiteral("b"),
                   SyncPreferenceJson::encodeBoolean(true));
        obj.insert(QStringLiteral("i"), SyncPreferenceJson::encodeInt(7));
        // Compose-side writer COULD emit int-typed values as JSON strings;
        // kotlinx intOrNull accepts numeric-content strings - mirror it.
        obj.insert(QStringLiteral("iStr"),
                   QJsonObject{{QLatin1String("type"),
                                QLatin1String("int")},
                               {QLatin1String("value"),
                                QStringLiteral("9")}});
        obj.insert(QStringLiteral("f"), SyncPreferenceJson::encodeFloat(0.25f));
        obj.insert(QStringLiteral("fStr"),
                   QJsonObject{{QLatin1String("type"),
                                QLatin1String("float")},
                               {QLatin1String("value"),
                                QStringLiteral("1e2")}});
        // Fractional number under int tag: toIntOrNull yields null.
        obj.insert(QStringLiteral("badI"),
                   QJsonObject{{QLatin1String("type"), QLatin1String("int")},
                               {QLatin1String("value"), 3.5}});
        // Wrong envelope tag -> rejected (int tag can't satisfy float decode).
        obj.insert(QStringLiteral("wrongT"),
                   QJsonObject{{QLatin1String("type"), QLatin1String("int")},
                               {QLatin1String("value"), 5}});
        // Missing type tag entirely -> rejected.
        obj.insert(QStringLiteral("missingTag"),
                   QJsonObject{{QLatin1String("value"), 5}});

        const auto K = [](const char* k) { return QString::fromUtf8(k); };
        CHECK(SyncPreferenceJson::decodeString(obj, K("s")) == QString("x"),
              "string decoded");
        CHECK(SyncPreferenceJson::decodeBoolean(obj, K("b")).value_or(false),
              "bool decoded");
        CHECK(SyncPreferenceJson::decodeInt(obj, K("i")) == 7,
              "numeric int accepted");
        CHECK(SyncPreferenceJson::decodeInt(obj, K("iStr")) == 9,
              "numeric-content string int accepted (intOrNull parity)");
        CHECK(!SyncPreferenceJson::decodeInt(obj, K("badI")).has_value(),
              "fractional int rejected (3.5 -> toIntOrNull null)");
        CHECK(SyncPreferenceJson::decodeFloat(obj, K("f")).value_or(-1) ==
                  0.25f,
              "float accepted");
        CHECK(SyncPreferenceJson::decodeFloat(obj, K("fStr")).value_or(-1) ==
                  100.0f,
              "scientific string float accepted");
        CHECK(!SyncPreferenceJson::decodeFloat(obj, K("wrongT")).has_value(),
              "wrong type tag rejected");
        CHECK(!SyncPreferenceJson::decodeInt(obj, K("missingTag"))
                   .has_value(),
              "missing type tag rejected");
        CHECK(!SyncPreferenceJson::decodeString(obj, K("absent")).has_value(),
              "absent key rejected");
    }

    { // round-trips + string_set trimming/dedup semantics
        // Decoders address entries by key INSIDE an outer object - wrap
        // envelopes exactly like the blob features maps do.
        const auto wrap =
            [](const QJsonObject& envelope) {
                return QJsonObject{{QStringLiteral("v"), envelope}};
            };

        const auto setObj = SyncPreferenceJson::encodeStringSet(
            QSet<QString>{QStringLiteral(" a "), QStringLiteral("a"),
                          QStringLiteral("")});
        const auto out = SyncPreferenceJson::decodeStringSet(
            wrap(setObj), QStringLiteral("v"));
        CHECK(out.has_value() && out->size() == 1 && out->contains("a"),
              "set trims, drops blanks, dedupes");

        CHECK(SyncPreferenceJson::decodeString(
                  wrap(SyncPreferenceJson::encodeString(QStringLiteral("ja"))),
                  QStringLiteral("v")) == QString("ja"),
              "string round-trip");
    }

    std::printf(failures ? "SYNC-PREFS SUITE FAILURES=%d\n"
                         : "SYNC-PREFS SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
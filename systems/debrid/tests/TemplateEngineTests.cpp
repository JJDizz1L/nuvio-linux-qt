// Template engine contract: placeholders, conditions, transforms,
// branches, scanners. Vectors mirror the default name-template shapes.
#include <nuvio/debrid/StreamTemplateEngine.h>

#include <QCoreApplication>

#include <cstdio>

using nuvio::debrid::StreamTemplateEngine;
using nuvio::debrid::TemplateBytes;

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
    StreamTemplateEngine engine;
    const auto render = [&](const QString& t, const QVariantMap& v) {
        return engine.render(t, v);
    };

    { // plain fields + missing degrades to empty, unterminated survives
        QVariantMap v{{"stream.resolution", "1080p"}};
        CHECK(render("A {stream.resolution} B", v) == "A 1080p B",
              "field renders");
        CHECK(render("A {stream.missing} B", v) == "A  B",
              "missing renders empty");
        CHECK(render("A {stream.resolution", v) == "A {stream.resolution",
              "unterminated survives");
        CHECK(render("", v).isEmpty(), "empty template");
    }

    { // the shipped default name template shape
        QVariantMap v{{"stream.resolution", "1080p"},
                      {"service.shortName", "TB"}};
        CHECK(render("{stream.resolution::exists[\"{stream.resolution} "
                     "\"||\"\"]}{service.shortName::exists["
                     "\"{service.shortName}\"||\"Cloud\"]} Instant",
                     v) == "1080p TB Instant",
              "default name template, all present");
        CHECK(render("{stream.resolution::exists[\"{stream.resolution} "
                     "\"||\"\"]}{service.shortName::exists["
                     "\"{service.shortName}\"||\"Cloud\"]} Instant",
                     {}) == "Cloud Instant",
              "default name template, all missing");
    }

    { // conditions: comparisons, contains, equality, and/or
        QVariantMap v{{"stream.size", 2048}, {"stream.encode", "HEVC"}};
        CHECK(render("{stream.size::>1024[\"big\"||\"small\"]}", v) == "big",
              "numeric gt");
        CHECK(render("{stream.size::=2048[\"exact\"||\"no\"]}", v) == "exact",
              "numeric eq");
        CHECK(render("{stream.encode::=hevc[\"match\"||\"no\"]}", v) == "match",
              "text eq case-insensitive");
        CHECK(render("{stream.encode::~evc[\"has\"||\"no\"]}", v) == "has",
              "contains");
        CHECK(render("{stream.missing::exists[\"y\"||\"n\"]}", v) == "n",
              "exists on missing");
        CHECK(render("{stream.encode::exists::and::stream.size::>100["
                     "\"y\"||\"n\"]}",
                     v) == "y",
              "and group");
        CHECK(render("{stream.missing::exists::or::stream.encode::exists["
                     "\"y\"||\"n\"]}",
                     v) == "y",
              "or group");
        // Truthiness follows TYPE: numeric string "0" is truthy (exists).
        CHECK(render("{stream.zero::istrue[\"y\"||\"n\"]}",
                     QVariantMap{{"stream.zero", "0"}}) == "n",
              "string 0 is not istrue");
    }

    { // transforms
        QVariantMap v{{"a", "hELLo WoRLD"},
                      {"n", 1536},
                      {"list", QStringList{"b", "", "a"}},
                      {"secs", 3661}};
        CHECK(render("{a::title}", v) == "Hello World", "title");
        CHECK(render("{a::lower}", v) == "hello world", "lower");
        CHECK(render("{a::upper}", v) == "HELLO WORLD", "upper");
        CHECK(render("{n::bytes}", v) == "1.5 KB", "bytes tenths");
        CHECK(render("{secs::time}", v) == "1h 1m", "time hours");
        CHECK(render("{list::join(' | ')}", v) == "b | a", "join skips blank");
        CHECK(render("{a::replace('o', '0')}", v) == "hELL0 W0RLD",
              "replace");
        CHECK(render("{n::bytes}", QVariantMap{{"n", 500}}) == "500 B",
              "bytes small");
    }

    { // branches without separator + byte values
        QVariantMap v;
        v.insert("stream.size",
                 QVariant::fromValue(TemplateBytes{5LL * 1024 * 1024}));
        CHECK(render("{stream.size}", v) == "5 MB", "bytes value renders");
        CHECK(render("{stream.size::exists[\"S\"]}", v) == "S",
              "exists on bytes");
        CHECK(render("{stream.size::istrue[\"S\"||\"N\"]}",
                     QVariantMap{{"stream.size", 0}}) == "N",
              "zero bytes falsy");
    }

    std::printf(failures ? "TEMPLATE SUITE FAILURES=%d\n"
                         : "TEMPLATE SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}

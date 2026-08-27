// Offline contract for the Discord wire layer: frame codec + payload
// builders (the runtime manager needs a live socket and is covered by the
// launch-check, not this suite).
#include <nuvio/integrations/DiscordRpc.h>

#include <QCoreApplication>
#include <cstdio>

using namespace nuvio::integrations::discord;
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

    {
        const QByteArray json = "{\"a\":1}";
        const QByteArray f    = buildFrame(OpFrame, json);
        CHECK(f.size() == 8 + json.size(), "frame = 8 header + json");
        CHECK(static_cast<quint8>(f[0]) == 1
                  && static_cast<quint8>(f[4]) == json.size(),
              "op + length little-endian");

        int consumed = 0;
        const auto parsed = parseFrame(f, &consumed);
        CHECK(parsed.has_value() && consumed == f.size(), "roundtrip parse");
        if (parsed) {
            CHECK(parsed->first == OpFrame, "opcode preserved");
            CHECK(parsed->second == json, "payload preserved");
        }
    }
    { // pipelined frames drain via consumedOut
        const QByteArray two =
            buildFrame(OpPing, "{}") + buildFrame(OpPong, "{\"x\":2}");
        int consumed = 0;
        auto first = parseFrame(two, &consumed);
        CHECK(first.has_value() && first->first == OpPing,
              "first pipelined frame");
        auto second = parseFrame(two.mid(consumed), &consumed);
        CHECK(second.has_value() && second->first == OpPong,
              "second frame drains cleanly");
    }
    {
        CHECK(!parseFrame(QByteArray(7, 'x')).has_value(),
              "short buffer rejected");
        QByteArray badLen = buildFrame(OpFrame, "{}");
        badLen[4] = static_cast<char>(0x7f);   // absurd length
        CHECK(!parseFrame(badLen).has_value(),
              "corrupt length rejected (64k guard)");
    }
    {
        const QByteArray body = buildSetActivity(
            QStringLiteral("n-42"), QStringLiteral("Title"), QString(),
            1700000000, 1700003000);
        CHECK(body.contains("\"cmd\":\"SET_ACTIVITY\""), "cmd field");
        CHECK(body.contains("\"nonce\":\"n-42\""), "nonce present");
        CHECK(body.contains("\"state\":\"Title\""), "state text");
        CHECK(body.contains("\"start\":1700000000")
                  && body.contains("\"end\":1700003000"),
              "timestamps encoded");
        const QByteArray paused =
            buildSetActivity(QStringLiteral("n"), QStringLiteral("t"),
                             QString());
        CHECK(!paused.contains("timestamps"),
              "paused omits timestamps entirely");
    }

    std::printf(failures ? "DISCORD SUITE FAILURES=%d\n"
                         : "DISCORD SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}


// Offline contract for the resolver's synchronous-facing surface: the
// resolving property lifecycle and the terminal-signal routing (the invalid
// URL path emits trailerFailed synchronously and leaves the resolver idle;
// a valid key demotes to a worker thread and is exercised against network,
// which this suite deliberately does NOT do - keep it offline/deterministic).
#include <nuvio/trailer/TrailerResolver.h>

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <cstdio>

using namespace nuvio::trailer;
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
    TrailerResolver r;

    int failedCount = 0;
    int resolvedCount = 0;
    QObject::connect(&r, &TrailerResolver::trailerFailed,
                     [&failedCount](const QString&) { ++failedCount; });
    QObject::connect(&r, &TrailerResolver::trailerResolved,
                     [&resolvedCount](const QString&, const QString&) {
                         ++resolvedCount;
                     });

    CHECK(!r.isResolving(), "resolver starts idle");

    // Invalid key: rejected before any worker spawn, so trailerFailed fires
    // synchronously and resolving never leaves idle.
    r.resolveForKey(QString());
    CHECK(failedCount == 1 && resolvedCount == 0,
          "blank key -> trailerFailed");
    CHECK(!r.isResolving(), "invalid key leaves resolver idle");

    r.resolveForKey(QStringLiteral("not-a-youtube-id-!!!!"));
    CHECK(failedCount == 2 && resolvedCount == 0,
          "garbage key -> trailerFailed");
    CHECK(!r.isResolving(), "garbage key leaves resolver idle");

    std::printf(failures ? "RESOLVER SUITE FAILURES=%d\n"
                         : "RESOLVER SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
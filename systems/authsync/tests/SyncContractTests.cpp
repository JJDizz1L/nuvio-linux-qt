// PRODUCTION contract tests for the profile-settings sync RPCs.
// Gate: runs ONLY with NUVIO_SYNC_CONTRACT=1; otherwise exits 0 as SKIP so
// offline ctest stays hermetic. Credentials come from the standard chain
// (AuthConfig::load): NUVIO_SUPABASE_URL/ANON_KEY env or local.properties.
//
// Tier 0 (no credentials needed): call both RPCs signed-out. Accepts ANY
// definitive HTTP verdict but requires valid JSON when 2xx - proves route
// existence + param schema without mutating anything.
//
// Tier 1 (NUVIO_SYNC_CONTRACT_EMAIL + NUVIO_SYNC_CONTRACT_PASSWORD also
// set): signs in through the PRODUCTION AuthService flow, then does a full
// push->pull round-trip on RESERVED THROWAWAY PROFILE ID 900001 (never a
// real user profile; convention documented in AGENTS.md). Writes a random
// marker language server-side and verifies the echo comes back identical.
#include <nuvio/authsync/AuthService.h>
#include <nuvio/authsync/SyncRpcClient.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

using nuvio::authsync::AuthConfig;
using nuvio::authsync::AuthService;
using nuvio::authsync::SyncRpcClient;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {

constexpr int kContractProfileId = 900001;   // reserved: never a real user

struct RpcOutcome {
    bool fired = false;
    bool ok = false;
    int status = 0;
    QJsonDocument doc;
};

bool runRpc(SyncRpcClient& client, const QString& fn,
            const QJsonObject& params, RpcOutcome* out)
{
    QEventLoop loop;
    QTimer bail;
    bail.setSingleShot(true);
    QObject::connect(&bail, &QTimer::timeout, &loop, &QEventLoop::quit);
    auto con = QObject::connect(
        &client, &SyncRpcClient::finished,
        [&](bool ok, int status, const QJsonDocument& doc, QByteArray) {
            out->fired = true; out->ok = ok;
            out->status = status; out->doc = doc;
            loop.quit();
        });
    bail.start(25000);
    client.call(fn, params);
    loop.exec();
    QObject::disconnect(con);
    return out->fired;
}

QString envelopeValue(const QJsonDocument& pullDoc, const QString& key)
{
    // pull -> [{profile_id, settings_json:{version,features:{player_settings:{key:{type,value}}}}}]
    QJsonObject row;
    if (pullDoc.isArray()) {
        const QJsonArray rows = pullDoc.array();
        if (!rows.isEmpty()) row = rows.first().toObject();
    } else {
        row = pullDoc.object();
    }
    return row.value(QStringLiteral("settings_json"))
        .toObject()
        .value(QStringLiteral("features"))
        .toObject()
        .value(QStringLiteral("player_settings"))
        .toObject()
        .value(key)
        .toObject()
        .value(QStringLiteral("value"))
        .toString();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    if (!qEnvironmentVariableIsSet("NUVIO_SYNC_CONTRACT")) {
        std::printf(
            "SYNC-CONTRACT SKIPPED (set NUVIO_SYNC_CONTRACT=1 to run)\n");
        return 0;
    }

    const AuthConfig cfg = AuthConfig::load();
    if (!cfg.valid()) {
        std::fprintf(stderr,
                     "FAIL supabase config missing (env/local.properties)\n");
        std::printf("SYNC-CONTRACT SUITE FAILURES=%d\n", 1);
        return 1;
    }
    std::fprintf(stderr, "CONTRACT target: %s\n", cfg.baseUrl.constData());

    { // ---- Tier 0: signed-out reachability (no mutation expectations) ------
        SyncRpcClient anon(cfg, [] { return QByteArray(); });

        RpcOutcome out;
        CHECK(runRpc(anon,
                     QString::fromLatin1(nuvio::authsync::SyncFn::kPullProfileBlob),
                     QJsonObject{{QStringLiteral("p_profile_id"),
                                  kContractProfileId},
                                 {QStringLiteral("p_platform"),
                                  QStringLiteral("desktop")}},
                     &out),
              "tier0 pull got an HTTP verdict");
        std::fprintf(stderr, "TIER0 pull: ok=%d status=%d body=%s\n",
                     int(out.ok), out.status,
                     out.doc.toJson(QJsonDocument::Compact).constData());
        CHECK(!out.ok || out.doc.isArray() || out.doc.isObject(),
              "tier0 2xx body parses as JSON");

        RpcOutcome pout;
        CHECK(runRpc(anon,
                     QString::fromLatin1(nuvio::authsync::SyncFn::kPushProfileBlob),
                     QJsonObject{{QStringLiteral("p_profile_id"),
                                  kContractProfileId},
                                 {QStringLiteral("p_platform"),
                                  QStringLiteral("desktop")}},
                     &pout),
              "tier0 push got an HTTP verdict");
        std::fprintf(stderr, "TIER0 push: ok=%d status=%d body=%s\n",
                     int(pout.ok), pout.status,
                     pout.doc.toJson(QJsonDocument::Compact).constData());
        CHECK(pout.status < 500, "tier0 push not a server error");
    }

    // ---- Tier 1: authenticated round-trip (needs explicit credentials) -----
    const QString email = qEnvironmentVariable("NUVIO_SYNC_CONTRACT_EMAIL");
    const QString password =
        qEnvironmentVariable("NUVIO_SYNC_CONTRACT_PASSWORD");
    if (!email.isEmpty() && !password.isEmpty()) {
        // Isolate any token writes away from the developer profile.
        QTemporaryDir sandbox;
        if (!sandbox.isValid()) return 2;
        qputenv("XDG_CONFIG_HOME",
                QDir(sandbox.path()).filePath("cfg").toUtf8());
        QDir().mkpath(QString::fromUtf8(qgetenv("XDG_CONFIG_HOME")));

        AuthService auth;
        auth.restoreSession();

        bool signInDone = false;
        auth.signIn(email, password);

        QEventLoop loop;
        QTimer bail;
        bail.setSingleShot(true);
        QObject::connect(&bail, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&auth, &AuthService::authResult,
                         [&](bool, const QString&) {
                             signInDone = true;
                             loop.quit();
                         });
        bail.start(25000);
        if (!signInDone) loop.exec();
        const bool signedIn = signInDone && auth.sessionActive();
        CHECK(signedIn, "tier1 sign-in succeeded");

        if (signedIn) {
            SyncRpcClient authed(cfg,
                                 [&auth] { return auth.accessToken(); });

            const QString marker =
                QStringLiteral("ct-%1")
                    .arg(QRandomGenerator::global()->bounded(1000000));
            const QJsonObject playerFragment{
                {QStringLiteral("preferred_audio_language_1"),
                 QJsonObject{{QLatin1String("type"),
                              QLatin1String("string")},
                             {QLatin1String("value"), marker}}}};

            RpcOutcome pout;
            const bool pushed = runRpc(
                authed, QString::fromLatin1(nuvio::authsync::SyncFn::kPushProfileBlob),
                QJsonObject{
                    {QStringLiteral("p_profile_id"), kContractProfileId},
                    {QStringLiteral("p_platform"),
                     QStringLiteral("desktop")},
                    {QStringLiteral("p_origin_client_id"),
                     QStringLiteral("nuvio-mobile-contract-test")},
                    {QStringLiteral("p_settings_json"),
                     QJsonObject{{QStringLiteral("version"), 3},
                                 {QStringLiteral("features"),
                                  QJsonObject{{QStringLiteral(
                                                   "player_settings"),
                                               playerFragment}}}}}},
                &pout);
            CHECK(pushed && pout.ok, "tier1 push accepted by production");
            std::fprintf(stderr, "TIER1 push: ok=%d status=%d\n",
                         int(pout.ok), pout.status);

            if (pushed && pout.ok) {
                RpcOutcome gout;
                const bool pulled = runRpc(
                    authed, QString::fromLatin1(nuvio::authsync::SyncFn::kPullProfileBlob),
                    QJsonObject{{QStringLiteral("p_profile_id"),
                                 kContractProfileId},
                                {QStringLiteral("p_platform"),
                                 QStringLiteral("desktop")}},
                    &gout);
                CHECK(pulled && gout.ok, "tier1 pull accepted");

                const QString echoed = envelopeValue(
                    gout.doc,
                    QStringLiteral("preferred_audio_language_1"));
                std::fprintf(stderr, "TIER1 echoed=%s want=%s\n",
                             echoed.toUtf8().constData(),
                             marker.toUtf8().constData());
                CHECK(echoed == marker,
                      "tier1 round-trip echoes exact marker");
            }
        }
    } else {
        std::printf(
            "TIER1 skipped (set NUVIO_SYNC_CONTRACT_EMAIL/_PASSWORD)\n");
    }

    std::printf("SYNC-CONTRACT SUITE FAILURES=%d\n", failures);
    return failures ? 1 : 0;
}
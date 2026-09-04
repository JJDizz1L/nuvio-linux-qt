// Offline contract for membership: model/codec/parse rules, community
// normalize/sort/format, and the access+overview state machines against
// a local RPC fake (XDG-sandboxed; Supabase + nuvio.tv never touched).
#include <nuvio/authsync/AuthService.h>
#include <nuvio/membership/CommunityService.h>
#include <nuvio/membership/MemberAccess.h>
#include <nuvio/membership/Membership.h>
#include <nuvio/membership/MembershipOverview.h>
#include <nuvio/settings/PropertiesStore.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

using nuvio::authsync::AuthService;
using nuvio::membership::CommunityService;
using nuvio::membership::decodeStoredAccess;
using nuvio::membership::encodeStoredAccess;
using nuvio::membership::entitlementFromName;
using nuvio::membership::entitlementName;
using nuvio::membership::formatDonationDate;
using nuvio::membership::formatMembershipLevel;
using nuvio::membership::contributorSupportLink;
using nuvio::membership::CosmeticEntitlement;
using nuvio::membership::MemberAccess;
using nuvio::membership::MembershipOverview;
using nuvio::membership::MemberTier;
using nuvio::membership::memberTierDisplayName;
using nuvio::membership::memberTierFromName;
using nuvio::membership::memberTierName;
using nuvio::membership::normalizeContributor;
using nuvio::membership::normalizeSupporter;
using nuvio::membership::parseMemberAccess;
using nuvio::membership::parseMembershipOverview;
using nuvio::membership::parseContributors;
using nuvio::membership::parseSupportersWall;
using nuvio::membership::sortContributors;
using nuvio::membership::CommunityContributor;
using nuvio::membership::CommunitySupporter;

namespace {

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Minimal RPC fake: routes postgrest fn calls to canned rows.
class FakeRpc final : public QObject {
public:
    QString accessReply = "[]";
    QString overviewReply = "[]";
    int accessHits = 0;
    int overviewHits = 0;

    bool start()
    {
        connect(&m_srv, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* sock = m_srv.nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
                m_buf[sock] += sock->readAll();
                const int split = m_buf[sock].indexOf("\r\n\r\n");
                if (split < 0) return;
                const QByteArray raw = m_buf[sock];
                m_buf.remove(sock);
                const QString head =
                    QString::fromUtf8(raw.left(split)).section("\r\n", 0, 0);
                const QString path =
                    head.section(' ', 1, 1).section('?', 0, 0);
                QByteArray body;
                if (path.endsWith("/rpc/get_my_member_access")) {
                    ++accessHits;
                    body = accessReply.toUtf8();
                } else if (path.endsWith(
                               "/rpc/get_my_membership_overview")) {
                    ++overviewHits;
                    body = overviewReply.toUtf8();
                } else {
                    body = "[]";
                }
                const QByteArray out =
                    QByteArray("HTTP/1.1 200 OK\r\nContent-Type: "
                               "application/json\r\nContent-Length: ") +
                    QByteArray::number(body.size()) +
                    QByteArray("\r\nConnection: close\r\n\r\n") + body;
                sock->write(out);
                sock->flush();
                sock->disconnectFromHost();
            });
        });
        if (!m_srv.listen(QHostAddress::LocalHost, 0)) return false;
        qputenv("NUVIO_SUPABASE_URL",
                QStringLiteral("http://127.0.0.1:%1")
                    .arg(m_srv.serverPort())
                    .toUtf8());
        qputenv("NUVIO_SUPABASE_ANON_KEY", "test-anon");
        return true;
    }

private:
    QTcpServer m_srv;
    QHash<QTcpSocket*, QByteArray> m_buf;
};

void seedSignedIn()
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("auth"));
    store.putString("access_token", "tok");
    store.putString("refresh_token", "ref");
    store.putString("user_email", "fan@nuvio.tv");
    store.putString("user_id", "u1");
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

    { // model + codec + parse rules
        CHECK(memberTierName(MemberTier::Supporter) == "SUPPORTER",
              "tier name");
        CHECK(memberTierName(MemberTier::None).isEmpty(), "none unnamed");
        CHECK(memberTierFromName("SUPPORTER_PLUS") ==
                  MemberTier::SupporterPlus,
              "tier parse");
        CHECK(memberTierFromName("BOGUS") == MemberTier::None,
              "unknown tier parses None");
        CHECK(memberTierDisplayName(MemberTier::SupporterPlus) ==
                  "Supporter Plus",
              "tier display");
        CHECK(entitlementName(CosmeticEntitlement::ProfileAvatars) ==
                  "PROFILE_AVATARS",
              "entitlement name");
        CosmeticEntitlement e;
        CHECK(entitlementFromName("GOLD_THEME", e) &&
                  e == CosmeticEntitlement::GoldTheme,
              "entitlement parse");
        CHECK(!entitlementFromName("BOGUS", e), "unknown entitlement refused");
        const QString enc = encodeStoredAccess(
            "u1", MemberTier::Supporter,
            {CosmeticEntitlement::GoldTheme});
        const auto dec = decodeStoredAccess(enc);
        CHECK(dec.valid && dec.userId == "u1" &&
                  dec.tier == MemberTier::Supporter &&
                  dec.entitlements.size() == 1,
              "payload round-trips");
        CHECK(!decodeStoredAccess("").valid, "empty payload invalid");
        CHECK(!decodeStoredAccess("{garbage").valid, "garbage invalid");
        CHECK(!decodeStoredAccess(
                   "{\"userId\":\"u\",\"tier\":\"BOGUS\",\"entitlements\":[]}")
                   .valid,
              "unknown tier invalidates");
        MemberTier tier = MemberTier::Supporter;
        QList<CosmeticEntitlement> ents;
        CHECK(parseMemberAccess("[]", tier, ents) &&
                  tier == MemberTier::None && ents.isEmpty(),
              "empty access rows decode None");
        CHECK(parseMemberAccess(
                  "[{\"tier\":\"SUPPORTER\",\"entitlements\":[\"JADE_THEME\","
                  "\"BOGUS\"]}]",
                  tier, ents) &&
                  tier == MemberTier::Supporter && ents.size() == 1,
              "access rows decode, unknown entitlements dropped");
        const auto ov = parseMembershipOverview("[]");
        CHECK(!ov.active() && ov.status == "inactive",
              "empty overview inactive");
        const auto ov2 = parseMembershipOverview(
            "[{\"status\":\"active\",\"tier\":\"SUPPORTER_PLUS\","
            "\"supporter_since\":\"2024-03-01T00:00:00Z\","
            "\"has_subscription\":true,"
            "\"subscription_access_active\":true,"
            "\"cancels_at_period_end\":true,"
            "\"has_active_grant\":true,\"grant_is_lifetime\":false,"
            "\"grant_tier\":\"SUPPORTER\",\"grant_kind\":\"promo\","
            "\"has_lifetime_grant\":false}]");
        CHECK(ov2.active() && ov2.tier == MemberTier::SupporterPlus,
              "overview active + tier");
        CHECK(ov2.subscriptionActive && ov2.cancelsAtPeriodEnd,
              "subscription flags");
        CHECK(ov2.hasActiveGrant && !ov2.grantIsLifetime &&
                  ov2.grantTier == MemberTier::Supporter &&
                  ov2.grantExpiresAt.isEmpty(),
              "grant legs (empty expiry stays empty)");
        CHECK(!ov2.hasLifetimeGrant &&
                  ov2.lifetimeGrantTier == MemberTier::None,
              "no lifetime grant");
        CHECK(formatDonationDate("2024-03-01T00:00:00Z").endsWith("2024") &&
                  formatDonationDate("2024-03-01T00:00:00Z")
                          .contains("1,"),
              "donation date shapes");
        CHECK(formatDonationDate("nonsense") == "nonsense",
              "bad dates pass through");
    }

    { // community normalize/sort/parse/format rules
        CommunityContributor c;
        CHECK(!normalizeContributor("", "", "", 5, c), "blank login dropped");
        CHECK(!normalizeContributor(" dev ", "", "", 0, c),
              "zero contributions dropped");
        CHECK(normalizeContributor(" dev ", "av", "prof", 42, c) &&
                  c.login == "dev" && c.totalContributions == 42,
              "contributor normalizes + trims");
        CommunitySupporter s;
        CHECK(!normalizeSupporter("  ", "", "", "", 0, s),
              "blank supporter dropped");
        CHECK(normalizeSupporter("Ada", "", "", "2024-01-02", 3, s) &&
                  s.membershipLevel == "SUPPORTER" &&
                  s.key == "ada-2024-01-02#3",
              "supporter defaults level + keys uniquely");
        const auto rows = sortContributors(
            QList<CommunityContributor>{c, CommunityContributor{
                                               QStringLiteral("zed"),
                                               QString(), QString(), 7}});
        CHECK(rows.size() == 2 &&
                  rows[0].toMap().value("login").toString() == "dev",
              "contributors sort contributions-first");
        const auto parsed = parseContributors(
            "{\"contributors\":[{\"name\":\"a\",\"total\":1},{\"name\":\"\","
            "\"total\":9},{\"name\":\"b\",\"total\":0}]}");
        CHECK(parsed.size() == 1 && parsed[0].login == "a",
              "contributor rows filter");
        const auto wall = parseSupportersWall(
            "{\"top\":{\"members\":[{\"displayName\":\"Zed\","
            "\"membershipLevel\":\"supporter_plus\"},{\"displayName\":\"\"}]}}");
        CHECK(wall.size() == 1 && wall[0].membershipLevel == "supporter_plus",
              "wall rows filter blanks");
        CHECK(formatMembershipLevel("SUPPORTER_PLUS") == "Supporter Plus",
              "level formats");
        CHECK(formatMembershipLevel("") == "", "blank level stays blank");
        CHECK(contributorSupportLink("Skoruppa") ==
                  "https://ko-fi.com/skoruppa",
              "known support link (case-insensitive)");
        CHECK(contributorSupportLink("stranger").isEmpty(),
              "unknown logins have no link");
    }

    { // access state machine: signed-out None, signed-in verify, clear
        FakeRpc rpc;
        CHECK(rpc.start(), "member rpc listens");
        rpc.accessReply = "[{\"tier\":\"SUPPORTER_PLUS\",\"entitlements\":["
                          "\"GOLD_THEME\",\"PROFILE_AVATARS\",\"BOGUS\"]}]";
        AuthService auth;
        MemberAccess access(&auth);
        access.refresh();   // signed out: hydrates nothing, fetches nothing
        pump(200);
        CHECK(!access.active() && access.tierName().isEmpty(),
              "signed-out access reads None");
        CHECK(rpc.accessHits == 0, "no verification while signed out");
        seedSignedIn();
        auth.restoreSession();
        CHECK(auth.sessionActive(), "seeded session restores offline");
        access.refresh();
        pump(400);
        CHECK(access.active(), "verified access activates");
        CHECK(access.tierName() == "SUPPORTER_PLUS", "tier lands");
        CHECK(access.hasEntitlement("GOLD_THEME") &&
                  access.hasEntitlement("profile_avatars") &&
                  !access.hasEntitlement("JADE_THEME") &&
                  !access.hasEntitlement("BOGUS"),
              "entitlement gate (case-insensitive, unknown refused)");
        CHECK(rpc.accessHits >= 1, "verification hit the wire");
        nuvio::settings::PropertiesStore store(
            nuvio::settings::PropertiesStore::defaultPath("member_access"));
        const auto raw = store.getString("access_payload");
        CHECK(raw.has_value() &&
                  decodeStoredAccess(QString::fromStdString(*raw)).userId ==
                      "u1",
              "verified access persists userId-keyed");
        auth.signOut();
        pump(100);
        CHECK(!access.active(), "sign-out clears access");
    }

    { // overview state machine: inactive signed-out, live signed-in
        FakeRpc rpc2;
        CHECK(rpc2.start(), "overview rpc listens");
        rpc2.overviewReply = "[{\"status\":\"active\",\"tier\":\"SUPPORTER\","
                             "\"supporter_since\":\"2023-05-06\"}]";
        AuthService auth;
        MembershipOverview overview(&auth);
        overview.refresh();
        pump(200);
        CHECK(!overview.loading() && !overview.overview().isEmpty() &&
                  !overview.overview().value("active").toBool(),
              "signed-out overview rests inactive");
        CHECK(rpc2.overviewHits == 0, "no overview fetch while signed out");
        seedSignedIn();
        auth.restoreSession();
        overview.refresh();
        pump(400);
        CHECK(!overview.loading() &&
                  overview.overview().value("active").toBool(),
              "signed-in overview activates");
        CHECK(overview.overview().value("tierDisplay").toString() ==
                  "Supporter",
              "tier display rides the variant");
        CHECK(!overview.overview()
                   .value("supporterSince")
                   .toString()
                   .isEmpty(),
              "since date formatted");
        CHECK(rpc2.overviewHits >= 1, "overview hit the wire");
        CHECK(overview.errorMessage().isEmpty(), "no error on success");
    }

    std::printf(failures ? "MEMBERSHIP SUITE FAILURES=%d\n"
                         : "MEMBERSHIP SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}

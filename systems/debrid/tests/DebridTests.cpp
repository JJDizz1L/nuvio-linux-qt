// Debrid D1 contract: provider table, settings keys/defaults, wire shapes.
#include <nuvio/debrid/DebridApi.h>
#include <nuvio/debrid/DebridSettings.h>
#include <nuvio/debrid/DebridTypes.h>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cstdio>

using nuvio::debrid::DebridSettings;

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

    using namespace nuvio::debrid;

    { // provider table (verbatim ids/caps/auth methods)
        const auto providers = allProviders();
        CHECK(providers.size() == 3, "three providers");
        CHECK(providers[0].id == "torbox" &&
                  providers[0].authMethod == ProviderAuthMethod::DeviceCode,
              "torbox device flow");
        CHECK(providers[1].id == "premiumize" &&
                  providers[1].authMethod == ProviderAuthMethod::DeviceCode,
              "premiumize device flow");
        CHECK(providers[2].id == "realdebrid" &&
                  providers[2].authMethod == ProviderAuthMethod::ApiKey &&
                  !providers[2].visibleInUi,
              "realdebrid key-only, hidden");
        CHECK(providerHas(providers[0],
                          ProviderCapability::CloudLibrary) &&
                  !providerHas(providers[2],
                               ProviderCapability::CloudLibrary),
              "cloud capability matrix");
        CHECK(providerKeyId("TorBox") == "torbox", "key id normalizes");
        CHECK(providerApiKeyName("torbox") == "debrid_torbox_api_key",
              "per-provider key name");
        CHECK(providerApiKeyName("realdebrid") ==
                  "debrid_realdebrid_api_key",
              "realdebrid key name");
    }

    { // settings defaults + round-trips + enum validation
        DebridSettings s;
        CHECK(!s.enabled(), "disabled default");
        CHECK(s.cloudLibraryEnabled(), "cloud library default on");
        CHECK(s.preferredResolverProviderId().isEmpty(), "no resolver default");
        CHECK(s.streamSortMode() == "DEFAULT", "sort default");
        CHECK(s.streamMinimumQuality() == "ANY", "quality default");
        CHECK(s.streamNameTemplate().contains("{stream.resolution"),
              "verbatim default name template");
        CHECK(s.streamDescriptionTemplate().isEmpty(),
              "empty description template default");
        CHECK(s.providerApiKey("torbox").isEmpty(), "no key default");
        s.setEnabled(true);
        s.setStreamSortMode("SIZE_DESC");
        s.setStreamSortMode("BOGUS");
        s.setProviderApiKey("torbox", "k1");
        CHECK(s.enabled() && s.streamSortMode() == "SIZE_DESC",
              "round-trips; invalid enum rejected");
        CHECK(s.providerApiKey("TorBox") == "k1", "key lookup normalizes");

        nuvio::debrid::DebridSettings view;
        CHECK(view.enabled() && view.providerApiKey("torbox") == "k1",
              "settings persist across instances");
    }

    { // Torbox wire shapes
        CHECK(torbox::deviceStartUrl("nuvio") ==
                  "https://api.torbox.app/v1/api/user/auth/device/start"
                  "?app=nuvio",
              "device start url");
        const auto auth = torbox::parseDeviceAuthorization(
            QByteArray("{\"success\":true,\"data\":{\"device_code\":\"d\","
                       "\"code\":\"ABCD-EFGH\","
                       "\"friendly_verification_url\":\"https://t/x\","
                       "\"interval\":5}}"));
        CHECK(auth.valid() && auth.userCode == "ABCD-EFGH" &&
                  auth.verificationUrl == "https://t/x",
              "device auth parses (friendly url preferred)");
        CHECK(torbox::parseDeviceToken(
                  QByteArray("{\"success\":true,\"data\":{"
                             "\"access_token\":\"tok\"}}")) == "tok",
              "device token parses");
        CHECK(torbox::parseDeviceToken(
                  QByteArray("{\"success\":false}")).isEmpty(),
              "failed envelope yields no token");
        const auto cached = torbox::parseCheckCached(
            QByteArray("{\"success\":true,\"data\":{\"ABC\":"
                       "{\"name\":\"F\",\"size\":10}}}"));
        CHECK(cached.size() == 1 && cached.contains("abc") &&
                  cached.value("abc").value("name").toString() == "F",
              "checkcached keyed lowercase with rows");
        const QByteArray ccBody =
            torbox::checkCachedBody(QStringList{" AB ", "", "ab"});
        CHECK(ccBody.contains("\"hashes\":[\"ab\"]"), "hashes normalized");
    }

    { // Real-Debrid + Premiumize shapes
        CHECK(realdebrid::formBody({{"magnet", "magnet:?x=1&y=2"}}) ==
                  "magnet=magnet%3A%3Fx%3D1%26y%3D2",
              "form encoding");
        const auto added = realdebrid::parseAddMagnet(
            QByteArray("{\"id\":\"abc\",\"uri\":\"magnet:?x\"}"));
        CHECK(added.id == "abc", "addMagnet parses");
        CHECK(realdebrid::parseUnrestrictedDownload(
                  QByteArray("{\"download\":\"https://dl/x\"}")) ==
                  "https://dl/x",
              "unrestrict download link parses");
        const auto pauth = premiumize::parseDeviceAuthorization(
            QByteArray("{\"device_code\":\"d\",\"user_code\":\"U\","
                       "\"verification_uri_complete\":\"https://p/x\","
                       "\"expires_in\":900}"));
        CHECK(pauth.valid() && pauth.userCode == "U" &&
                  pauth.verificationUri == "https://p/x" &&
                  pauth.expiresInSec == 900,
              "premiumize device parses (complete uri preferred)");
        CHECK(premiumize::parseDeviceToken(
                  QByteArray(
                      "{\"access_token\":\"pt\",\"error\":\"\"}")) == "pt",
              "premiumize token parses");
        CHECK(premiumize::parseDeviceToken(
                  QByteArray("{\"error\":\"denied\"}")).isEmpty(),
              "premiumize error yields no token");
        CHECK(premiumize::accountOk(
                  QByteArray("{\"status\":\"success\"}")) &&
                  !premiumize::accountOk(
                      QByteArray("{\"status\":\"error\"}")),
              "account success flag");
        const auto cache = premiumize::parseCacheCheck(
            QByteArray("{\"status\":\"success\",\"response\":[true,false]}"));
        CHECK(cache.size() == 2 && cache[0] && !cache[1],
              "cache check aligns answers");
        CHECK(premiumize::parseCacheCheck(
                  QByteArray("{\"status\":\"error\"}")).isEmpty(),
              "failed cache check yields nothing");
    }

    std::printf(failures ? "DEBRID SUITE FAILURES=%d\n"
                         : "DEBRID SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}

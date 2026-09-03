// Profiles data contract: payload codec, PIN crypto, pin cache.
// ISOLATION: XDG sandbox (profiles + pin-cache files).
#include <nuvio/profiles/ProfileStore.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cstdio>

using nuvio::profiles::NuvioProfile;
using nuvio::profiles::ProfileCodec;
using nuvio::profiles::ProfilePinCache;

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

    using namespace nuvio::profiles;

    { // PIN crypto: sha256("profile:<i>:<salt>:<pin>"), 32-hex salts
        const QString digest = hashProfilePin(2, "abc", "1234");
        const QString expect = QString::fromLatin1(
            QCryptographicHash::hash("profile:2:abc:1234",
                                     QCryptographicHash::Sha256)
                .toHex());
        CHECK(digest == expect, "hash construction matches spec");
        CHECK(digest.size() == 64, "hex digest length");
        const QString s1 = generateProfilePinSalt();
        const QString s2 = generateProfilePinSalt();
        CHECK(!s1.isEmpty() && s1 != s2, "salts random");
    }

    { // payload codec: defaults, round-trip, garbage, index clamp
        const auto fresh =
            ProfileCodec::decodeStored(QStringLiteral("garbage"));
        CHECK(fresh.activeProfileIndex == 1 && fresh.profiles.isEmpty() &&
                  !fresh.hasEverSelectedProfile,
              "garbage decodes to defaults");

        NuvioProfile p;
        p.id = "uuid-1";
        p.userId = "user-9";
        p.profileIndex = 2;
        p.name = "Kids";
        p.avatarColorHex = "#FF0000";
        p.pinEnabled = true;
        StoredProfilesPayload payload;
        payload.userId = "user-9";
        payload.activeProfileIndex = 2;
        payload.hasEverSelectedProfile = true;
        payload.profiles.append(p);
        const QString enc = ProfileCodec::encodeStored(payload);
        CHECK(enc.contains("\"activeProfileIndex\":2") &&
                  enc.contains("\"pin_enabled\":true"),
              "encode shape");
        const auto back = ProfileCodec::decodeStored(enc);
        CHECK(back.userId == "user-9" && back.activeProfileIndex == 2 &&
                  back.hasEverSelectedProfile &&
                  back.profiles.size() == 1 &&
                  back.profiles[0].name == "Kids" &&
                  back.profiles[0].avatarColorHex == "#FF0000" &&
                  back.profiles[0].pinEnabled,
              "round-trip preserves rows and flags");

        const auto clamped = ProfileCodec::decodeStored(
            QStringLiteral("{\"activeProfileIndex\":9}"));
        CHECK(clamped.activeProfileIndex == 1, "index clamped to 1..6");

        const auto prof = ProfileCodec::profileFromJson(
            QJsonDocument::fromJson(
                QByteArray("{\"id\":\"u\",\"profile_index\":3,"
                           "\"name\":\"N\"}"))
                .object());
        CHECK(prof.profileIndex == 3 && prof.name == "N" &&
                  prof.avatarColorHex == "#1E88E5",
              "server row decodes with color default");

        const auto push = ProfileCodec::profileToPushJson(p);
        CHECK(push.value(QStringLiteral("profile_index")).toInt() == 2 &&
                  push.value(QStringLiteral("avatar_url")).isNull() &&
                  push.value(QStringLiteral("name")).toString() == "Kids",
              "push payload uses explicit nulls");
    }

    { // pin cache round-trip keyed per index
        CHECK(ProfilePinCache::loadPayload(2).isEmpty(), "cache cold");
        ProfilePinCache::savePayload(2, "{\"salt\":\"s\"}");
        CHECK(ProfilePinCache::loadPayload(2) == "{\"salt\":\"s\"}",
              "cache round-trips");
        CHECK(ProfilePinCache::loadPayload(3).isEmpty(),
              "indexes isolated");
        ProfilePinCache::removePayload(2);
        CHECK(ProfilePinCache::loadPayload(2).isEmpty(), "removal works");
    }

    std::printf(failures ? "PROFILES SUITE FAILURES=%d\n"
                         : "PROFILES SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}

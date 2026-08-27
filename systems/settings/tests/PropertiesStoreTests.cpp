// Headless-pure suite: no Qt needed. Codec + store persistence contract.
#include <nuvio/settings/PropertiesStore.h>

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace nuvio::settings;
static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s\n", msg); } } while(0)

static void parsesJavaEscapes()
{
    const std::string raw =
        "#Nuvio Linux desktop preferences\n"
        "#Tue Aug 26 13:48:57 EDT 2026\n"
        "addon_enabled_states_1={\"https\\\\://v3-cinemeta.strem.io\"\\:true}\n"
        "a=b\n";
    const auto m = PropertiesCodec::parse(raw);
    CHECK(m.size() == 2, "parse size");
    CHECK(m.count("addon_enabled_states_1") == 1, "key present");
    // Java-load parity: the profile bytes carry a DOUBLED backslash (store()
    // escaping the inner JSON's own "\\:"), so load retains ONE backslash.
    CHECK(m.at("addon_enabled_states_1").find(
              "https\\://v3-cinemeta.strem.io\":true") != std::string::npos,
          "colon unescaped in value JSON");
    CHECK(m.at("a") == "b", "simple pair");
}

static void roundTripSpecials()
{
    // Java parity in both directions:
    //  - a REAL space inside a key must be stored '\\'escaped ("k.sp\\ ace")
    //  - an UNESCAPED space splits key/value with no separator char
    const auto m = PropertiesCodec::parse(
        "k.sp\\ ace=va\\:lue#x\nk2=lead sp\n"
        "kws v2=after bare split\nkEmoji=caf\\u00e9 _ok\n");
    CHECK(m.count("k.sp ace") == 1, "space-in-key (escaped)");
    CHECK(m.at("k.sp ace") == "va:lue#x", "colon+hash value");
    CHECK(m.count("kws") == 1, "bare-ws split key");
    CHECK(m.at("kws") == "v2=after bare split", "bare-ws split value");
    CHECK(m.at("k2") == "lead sp", "leading-space value preserved");
    const auto back = PropertiesCodec::parse(PropertiesCodec::serialize(m));
    CHECK(back.size() == m.size(), "roundtrip size");
    for (const auto& [k, v] : m) {
        auto it = back.find(k);
        CHECK(it != back.end(), "roundtrip key");
        if (it != back.end()) CHECK(it->second == v, "roundtrip value");
    }
}

static void typedAccessorsPersist()
{
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "nuvio_qt_t";
    fs::create_directories(dir);
    const fs::path f = dir / "t.properties";
    fs::remove(f);
    {
        PropertiesStore s(f);
        s.putBoolean("b", true);
        s.putInt("i", -42);
        s.putFloat("f", 0.25f);
        s.putString("u", std::string("caf\xc3\xa9 _ok")); // real UTF-8 bytes
        s.putStringSet("set1", {"https://a.example/x", "b, with comma",
                                std::string("q\"uote\\back")});
        CHECK(s.getBoolean("b").value_or(false) == true, "read bool");
        CHECK(s.getInt("i").value_or(0) == -42, "read int");
    }
    PropertiesStore s2(f);                      // fresh = persistence proof
    CHECK(s2.getBoolean("b").value_or(false), "persisted bool");
    CHECK(s2.getInt("i").value_or(0) == -42, "persisted int");
    CHECK(s2.getFloat("f").value_or(0.f) == 0.25f, "persisted float");
    CHECK(s2.getString("u").value_or("").find("caf") == 0, "utf8 stored");
    const auto setBack = s2.getStringSet("set1");
    CHECK(setBack.has_value(), "set parses");
    if (setBack) {
        CHECK(setBack->size() == 3, "set size");
        CHECK((*setBack)[0] == "https://a.example/x", "set url item");
        CHECK((*setBack)[1] == "b, with comma", "set comma item");
        CHECK((*setBack)[2] == "q\"uote\\back", "set quote+backslash item");
    }
    struct ::stat st{};
    if (::stat(f.c_str(), &st) == 0)
        CHECK((st.st_mode & 0777) == 0600, "0600 perms parity");
    else CHECK(false, "file exists");
}

int main()
{
    parsesJavaEscapes();
    roundTripSpecials();
    typedAccessorsPersist();
    std::printf(failures ? "SETTINGS SUITE FAILURES=%d\n" : "SETTINGS SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}

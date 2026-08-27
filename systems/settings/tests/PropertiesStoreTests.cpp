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
    CHECK(m.at("addon_enabled_states_1").find(
              "https://v3-cinemeta.strem.io\":true") != std::string::npos,
          "colon unescaped in value JSON");
    CHECK(m.at("a") == "b", "simple pair");
}

static void roundTripSpecials()
{
    const auto m = PropertiesCodec::parse(
        "k.sp ace=va\\:lue#x\nk2=lead sp\nkEmoji=caf\\u00e9 _ok\n");
    CHECK(m.count("k.sp ace") == 1, "space-in-key");
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
        s.putString("u", "latin-1-caf-xC3-xA9 _ok");
        CHECK(s.getBoolean("b").value_or(false) == true, "read bool");
        CHECK(s.getInt("i").value_or(0) == -42, "read int");
    }
    PropertiesStore s2(f);                      // fresh = persistence proof
    CHECK(s2.getBoolean("b").value_or(false), "persisted bool");
    CHECK(s2.getInt("i").value_or(0) == -42, "persisted int");
    CHECK(s2.getFloat("f").value_or(0.f) == 0.25f, "persisted float");
    CHECK(s2.getString("u").value_or("").find("caf") == 0, "utf8 stored");
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

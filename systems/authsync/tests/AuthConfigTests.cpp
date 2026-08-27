// Headless contract surface that does NOT need the network. The LIVE
// backend contract (real sign-in against upstream Supabase) runs only when
// NUVIO_AUTH_LIVE=1 + valid creds are present - see AuthServiceLiveTests.
#include <nuvio/authsync/AuthConfig.h>

#include <cstdio>
#include <cstdlib>

using nuvio::authsync::AuthConfig;
static int failures = 0;
#define CHECK(cond, msg)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            ++failures;                                   \
            std::fprintf(stderr, "FAIL %s\n", msg);       \
        }                                                 \
    } while (0)

int main()
{
    { // endpoint derivation is single-source
        AuthConfig c;
        c.baseUrl = "https://api.nuvio.tv";
        c.anonKey = "k";
        CHECK(c.tokenUrl() ==
              "https://api.nuvio.tv/auth/v1/token?grant_type=password",
              "token url");
        CHECK(c.signupUrl() == "https://api.nuvio.tv/auth/v1/signup",
              "signup url");
        CHECK(c.refreshUrl().contains("grant_type=refresh_token"),
              "refresh grant");
        CHECK(c.userUrl() == "https://api.nuvio.tv/auth/v1/user", "user url");
    }
    { // invalid config detection
        AuthConfig c;
        CHECK(!c.valid(), "empty config invalid");
        c.baseUrl = "x";
        CHECK(!c.valid(), "missing key invalid");
    }
    { // env resolution wins and is byte-exact
        qputenv("NUVIO_SUPABASE_URL", "https://env.example");
        qputenv("NUVIO_SUPABASE_ANON_KEY", "envkey");
        const AuthConfig c = AuthConfig::load();
        CHECK(c.baseUrl == "https://env.example", "env url respected");
        CHECK(c.anonKey == "envkey", "env key respected");
        CHECK(c.valid(), "env-loaded valid");
        qunsetenv("NUVIO_SUPABASE_URL");
        qunsetenv("NUVIO_SUPABASE_ANON_KEY");
    }
    // local.properties discovery (walk-up from exe dir) cannot be exercised
    // portably inside ctest sandboxes; covered by live manual run instead.

    std::printf(failures ? "AUTHCFG SUITE FAILURES=%d\n"
                         : "AUTHCFG SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
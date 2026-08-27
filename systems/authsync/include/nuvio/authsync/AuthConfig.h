#pragma once

// Supabase auth configuration — L1 CONTAINMENT ZONE per plan §4.
//
// Resolution order (identical outcome everywhere, zero machine paths):
//   1. Environment:  NUVIO_SUPABASE_URL / NUVIO_SUPABASE_ANON_KEY
//   2. local.properties, found by walking UP from the executable's own
//      directory (packaged builds embed their values at build time later;
//      dev builds always live inside the repo tree they were built from)
//
// Server-facing identity MUST stay upstream-exact (rebranding rules,
// AGENTS.md): the base URL and anon key are upstream's shared backend
// identity, not NuvioLinux branding surface.

#include <QByteArray>

namespace nuvio::authsync {

struct AuthConfig {
    QByteArray baseUrl;
    QByteArray anonKey;

    [[nodiscard]] bool valid() const { return !baseUrl.isEmpty() && !anonKey.isEmpty(); }

    /// Absolute GoTrue endpoints derived from the base (single source).
    [[nodiscard]] QByteArray tokenUrl() const;    // /auth/v1/token?grant_type=password
    [[nodiscard]] QByteArray signupUrl() const;   // /auth/v1/signup
    [[nodiscard]] QByteArray refreshUrl() const;  // /auth/v1/token?grant_type=refresh_token
    [[nodiscard]] QByteArray userUrl() const;     // /auth/v1/user

    static AuthConfig load();
};

} // namespace nuvio::authsync
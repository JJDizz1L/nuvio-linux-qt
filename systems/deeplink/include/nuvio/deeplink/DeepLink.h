#pragma once

// App deeplink kernel (Appendix A, deeplinks): verbatim port of Compose's
// AppUrlBridge parse/build rules (Meta / AddonInstall / Downloads, auth
// reserved-null, stremio addon-host-only). QUrl replaces ktor Url; query
// items are percent-decoded on read like the fork.

#include <QString>

namespace nuvio::deeplink {

struct MetaLink {
    QString type;   // "movie" | "series"
    QString id;
};

struct AddonLink {
    QString manifestUrl;   // https://...
};

enum class DeepLinkKind { None, Meta, AddonInstall, Downloads };

struct DeepLink {
    DeepLinkKind kind = DeepLinkKind::None;
    MetaLink meta;
    AddonLink addon;
};

/// Parses nuvio:// + stremio:// urls. Anything else (or unparseable) is
/// None. Stremio links only ever resolve to AddonInstall (fork parity).
[[nodiscard]] DeepLink parseDeepLink(const QString& url);

/// "nuvio://meta?type=<type>&id=<id>" (buildMetaDeepLinkUrl parity).
[[nodiscard]] QString buildMetaUrl(const QString& type, const QString& id);
/// "nuvio://downloads" (buildDownloadsDeepLinkUrl parity).
[[nodiscard]] QString buildDownloadsUrl();

/// Entry filter (isDesktopAppUrl parity): only our two schemes.
[[nodiscard]] bool isAppUrl(const QString& value);

} // namespace nuvio::deeplink

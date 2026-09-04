#pragma once

// Debrid domain vocabulary (D1): verbatim Compose provider table
// (torbox/premiumize/realdebrid ids, display/short names, auth methods,
// capabilities) + settings value set. Pure data.

#include <QList>
#include <QString>

namespace nuvio::debrid {

enum class ProviderAuthMethod {
    ApiKey,
    DeviceCode,
};

enum class ProviderCapability {
    ClientResolve,
    LocalTorrentCacheCheck,
    LocalTorrentResolve,
    CloudLibrary,
};

struct DebridProvider {
    QString id;
    QString displayName;
    QString shortName;
    bool visibleInUi = true;
    ProviderAuthMethod authMethod = ProviderAuthMethod::ApiKey;
    QList<ProviderCapability> capabilities;
};

[[nodiscard]] inline QList<DebridProvider> allProviders()
{
    DebridProvider torbox;
    torbox.id = QStringLiteral("torbox");
    torbox.displayName = QStringLiteral("Torbox");
    torbox.shortName = QStringLiteral("TB");
    torbox.authMethod = ProviderAuthMethod::DeviceCode;
    torbox.capabilities = {ProviderCapability::ClientResolve,
                           ProviderCapability::LocalTorrentCacheCheck,
                           ProviderCapability::LocalTorrentResolve,
                           ProviderCapability::CloudLibrary};

    DebridProvider premiumize;
    premiumize.id = QStringLiteral("premiumize");
    premiumize.displayName = QStringLiteral("Premiumize");
    premiumize.shortName = QStringLiteral("PM");
    premiumize.authMethod = ProviderAuthMethod::DeviceCode;
    premiumize.capabilities = {ProviderCapability::ClientResolve,
                               ProviderCapability::LocalTorrentCacheCheck,
                               ProviderCapability::LocalTorrentResolve,
                               ProviderCapability::CloudLibrary};

    DebridProvider realdebrid;
    realdebrid.id = QStringLiteral("realdebrid");
    realdebrid.displayName = QStringLiteral("Real-Debrid");
    realdebrid.shortName = QStringLiteral("RD");
    realdebrid.visibleInUi = false;
    realdebrid.capabilities = {ProviderCapability::ClientResolve};

    return {torbox, premiumize, realdebrid};
}

[[nodiscard]] inline bool providerHas(
    const DebridProvider& provider, ProviderCapability capability)
{
    return provider.capabilities.contains(capability);
}

// Stream preference enums (Compose DebridSettings names verbatim).
inline const char* const kSortModes[] = {"DEFAULT", "QUALITY_DESC",
                                         "SIZE_DESC", "SIZE_ASC"};
inline const char* const kMinQualities[] = {"ANY", "P720", "P1080", "P2160"};
inline const char* const kFeatureFilters[] = {"ANY", "EXCLUDE", "ONLY"};
inline const char* const kCodecFilters[] = {"ANY", "H264", "HEVC", "AV1"};

} // namespace nuvio::debrid

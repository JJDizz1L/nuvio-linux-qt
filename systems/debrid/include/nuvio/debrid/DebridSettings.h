#pragma once

// Debrid settings (D1): verbatim Compose debrid_settings keys
// (profile-scoped, `debrid_` prefix; per-provider api keys as
// `debrid_<normalized>_api_key`; pending device payloads as
// `debrid_pending_device_authorization_<provider>`). QML-bound for the
// D3 settings page; the sync-blob debrid_settings fragment reads these
// same keys (wired in D2).

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace nuvio::debrid {

/// Normalized provider key id (known ids pass through; others lowercase
/// + sanitize verbatim Compose providerApiKeyKey).
[[nodiscard]] QString providerKeyId(const QString& providerId);
[[nodiscard]] QString providerApiKeyName(const QString& providerId);

class DebridSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled
                   NOTIFY changed)
    Q_PROPERTY(bool cloudLibraryEnabled READ cloudLibraryEnabled WRITE
                   setCloudLibraryEnabled NOTIFY changed)
    Q_PROPERTY(QString preferredResolverProviderId READ
                   preferredResolverProviderId WRITE
                       setPreferredResolverProviderId NOTIFY changed)
    Q_PROPERTY(int instantPlaybackPreparationLimit READ
                   instantPlaybackPreparationLimit WRITE
                       setInstantPlaybackPreparationLimit NOTIFY changed)
    Q_PROPERTY(int streamMaxResults READ streamMaxResults WRITE
                   setStreamMaxResults NOTIFY changed)
    Q_PROPERTY(QString streamSortMode READ streamSortMode WRITE
                   setStreamSortMode NOTIFY changed)
    Q_PROPERTY(QString streamMinimumQuality READ streamMinimumQuality WRITE
                   setStreamMinimumQuality NOTIFY changed)
    Q_PROPERTY(QString streamDolbyVisionFilter READ streamDolbyVisionFilter
                   WRITE setStreamDolbyVisionFilter NOTIFY changed)
    Q_PROPERTY(QString streamHdrFilter READ streamHdrFilter WRITE
                   setStreamHdrFilter NOTIFY changed)
    Q_PROPERTY(QString streamCodecFilter READ streamCodecFilter WRITE
                   setStreamCodecFilter NOTIFY changed)
    Q_PROPERTY(QString streamPreferences READ streamPreferences WRITE
                   setStreamPreferences NOTIFY changed)
    Q_PROPERTY(QString streamNameTemplate READ streamNameTemplate WRITE
                   setStreamNameTemplate NOTIFY changed)
    Q_PROPERTY(QString streamDescriptionTemplate READ
                   streamDescriptionTemplate WRITE
                       setStreamDescriptionTemplate NOTIFY changed)

public:
    explicit DebridSettings(QObject* parent = nullptr);

    [[nodiscard]] bool enabled() const;
    void setEnabled(bool v);
    [[nodiscard]] bool cloudLibraryEnabled() const;
    void setCloudLibraryEnabled(bool v);
    [[nodiscard]] QString preferredResolverProviderId() const;
    void setPreferredResolverProviderId(const QString& v);
    [[nodiscard]] int instantPlaybackPreparationLimit() const;
    void setInstantPlaybackPreparationLimit(int v);
    [[nodiscard]] int streamMaxResults() const;
    void setStreamMaxResults(int v);
    [[nodiscard]] QString streamSortMode() const;
    void setStreamSortMode(const QString& v);
    [[nodiscard]] QString streamMinimumQuality() const;
    void setStreamMinimumQuality(const QString& v);
    [[nodiscard]] QString streamDolbyVisionFilter() const;
    void setStreamDolbyVisionFilter(const QString& v);
    [[nodiscard]] QString streamHdrFilter() const;
    void setStreamHdrFilter(const QString& v);
    [[nodiscard]] QString streamCodecFilter() const;
    void setStreamCodecFilter(const QString& v);
    /// Opaque preferences blob (Compose DebridStreamPreferences JSON;
    /// modeled in D2 with the template engine).
    [[nodiscard]] QString streamPreferences() const;
    void setStreamPreferences(const QString& v);
    [[nodiscard]] QString streamNameTemplate() const;
    void setStreamNameTemplate(const QString& v);
    [[nodiscard]] QString streamDescriptionTemplate() const;
    void setStreamDescriptionTemplate(const QString& v);

    /// Per-provider API keys ("" when unset). Torbox/Premiumize primarily
    /// authorize via device code (D1 auth); keys persist for API-key flows.
    Q_INVOKABLE QString providerApiKey(const QString& providerId) const;
    Q_INVOKABLE void setProviderApiKey(const QString& providerId,
                                       const QString& apiKey);
    /// Ids with a stored key (resolver picker source).
    Q_INVOKABLE QStringList configuredProviderIds() const;

    /// Pending device-flow payloads (opaque JSON, per provider).
    [[nodiscard]] QString pendingDeviceAuthorization(
        const QString& providerId) const;
    void setPendingDeviceAuthorization(const QString& providerId,
                                       const QString& payload);
    void clearPendingDeviceAuthorization(const QString& providerId);

    /// Sync-blob debrid_settings fragment (present-keys export, tolerant
    /// per-key merge). NOTE: the export INCLUDES api keys; the orchestrator
    /// strips credential keys before assembling the blob (Compose
    /// credential-policy parity) - never push this fragment raw.
    [[nodiscard]] QJsonObject exportSyncPayload();
    bool applySyncPayload(const QJsonObject& payload);

signals:
    void changed();

private:
    [[nodiscard]] std::optional<QString> getString(const char* key) const;
    void putString(const char* key, const QString& value);
    [[nodiscard]] std::optional<bool> getBool(const char* key) const;
    void putBool(const char* key, bool value);
    [[nodiscard]] std::optional<int> getInt(const char* key) const;
    void putInt(const char* key, int value);
};

} // namespace nuvio::debrid

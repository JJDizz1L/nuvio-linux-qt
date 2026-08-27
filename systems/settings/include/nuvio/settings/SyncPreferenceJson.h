#pragma once

#include <QJsonObject>
#include <QSet>
#include <QString>

#include <optional>

namespace nuvio::settings {

/// Pure port of Compose core/sync/SyncPreferenceJson: the typed envelope
/// {"type":"string|boolean|int|float|string_set","value":...} used inside
/// profile-settings sync blobs. Encoders emit kotlinx-shaped JSON (asserted
/// literally in tests); decoders mirror Kotlin's *OrNull content-parsing
/// leniency (numeric primitives AND numeric-looking strings accepted).
///
/// Known acceptable divergence: kotlinx prints Float 1.0f as "1.0", Qt as
/// "1" - every consumer decodes generically, so the textual form never
/// carries meaning (no raw-bytes comparison anywhere in the protocol).
class SyncPreferenceJson final {
public:
    [[nodiscard]] static QJsonObject encodeString(const QString& value);
    [[nodiscard]] static QJsonObject encodeBoolean(bool value);
    [[nodiscard]] static QJsonObject encodeInt(int value);
    [[nodiscard]] static QJsonObject encodeFloat(float value);
    /// Encodes SORTED (Compose sorts before wrapping).
    [[nodiscard]] static QJsonObject encodeStringSet(
        const QSet<QString>& values);

    [[nodiscard]] static std::optional<QString> decodeString(
        const QJsonObject& obj, const QString& key);
    [[nodiscard]] static std::optional<bool> decodeBoolean(
        const QJsonObject& obj, const QString& key);
    [[nodiscard]] static std::optional<int> decodeInt(
        const QJsonObject& obj, const QString& key);
    [[nodiscard]] static std::optional<float> decodeFloat(
        const QJsonObject& obj, const QString& key);
    [[nodiscard]] static std::optional<QSet<QString>> decodeStringSet(
        const QJsonObject& obj, const QString& key);
};

} // namespace nuvio::settings
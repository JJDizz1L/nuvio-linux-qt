#include "nuvio/settings/SyncPreferenceJson.h"

#include <QJsonArray>

#include <algorithm>

namespace nuvio::settings {

namespace {
constexpr auto kTypeKey  = "type";
constexpr auto kValueKey = "value";

constexpr auto kTypeString    = "string";
constexpr auto kTypeBoolean   = "boolean";
constexpr auto kTypeInt       = "int";
constexpr auto kTypeFloat     = "float";
constexpr auto kTypeStringSet = "string_set";

// Kotlin JsonPrimitive.contentOrNull semantics: number primitives expose
// their literal text, strings their content, null/objects/arrays -> none.
[[nodiscard]] std::optional<QString> primitiveContent(const QJsonValue& v)
{
    if (v.isString()) return v.toString();
    if (v.isDouble())
        return QString::number(v.toDouble(), 'g', 17);
    return std::nullopt;
}

bool typeTagIs(const QJsonValue& entry, const char* expected)
{
    return entry.isObject() &&
           entry.toObject().value(QLatin1String(kTypeKey))
                   .toString() == QLatin1String(expected);
}
} // namespace

QJsonObject SyncPreferenceJson::encodeString(const QString& value)
{
    return QJsonObject{{QLatin1String(kTypeKey), QLatin1String(kTypeString)},
                       {QLatin1String(kValueKey), value}};
}

QJsonObject SyncPreferenceJson::encodeBoolean(bool value)
{
    return QJsonObject{{QLatin1String(kTypeKey), QLatin1String(kTypeBoolean)},
                       {QLatin1String(kValueKey), value}};
}

QJsonObject SyncPreferenceJson::encodeInt(int value)
{
    return QJsonObject{{QLatin1String(kTypeKey), QLatin1String(kTypeInt)},
                       {QLatin1String(kValueKey), value}};
}

QJsonObject SyncPreferenceJson::encodeFloat(float value)
{
    return QJsonObject{{QLatin1String(kTypeKey), QLatin1String(kTypeFloat)},
                       {QLatin1String(kValueKey), static_cast<double>(value)}};
}

QJsonObject SyncPreferenceJson::encodeStringSet(const QSet<QString>& values)
{
    QList<QString> sorted(values.cbegin(), values.cend());
    std::sort(sorted.begin(), sorted.end());
    QJsonArray arr;
    for (const auto& s : sorted) arr.append(s);
    return QJsonObject{{QLatin1String(kTypeKey),
                        QLatin1String(kTypeStringSet)},
                       {QLatin1String(kValueKey), arr}};
}

std::optional<QString> SyncPreferenceJson::decodeString(
    const QJsonObject& obj, const QString& key)
{
    const QJsonValue entry = obj.value(key);
    if (!typeTagIs(entry, kTypeString)) return std::nullopt;
    const QJsonValue v = entry.toObject().value(QLatin1String(kValueKey));
    if (!v.isString()) return std::nullopt;
    return v.toString();
}

std::optional<bool> SyncPreferenceJson::decodeBoolean(
    const QJsonObject& obj, const QString& key)
{
    const QJsonValue entry = obj.value(key);
    if (!typeTagIs(entry, kTypeBoolean)) return std::nullopt;
    const QJsonValue v = entry.toObject().value(QLatin1String(kValueKey));
    // Kotlin booleanOrNull accepts only JSON booleans.
    if (!v.isBool()) return std::nullopt;
    return v.toBool();
}

std::optional<int> SyncPreferenceJson::decodeInt(
    const QJsonObject& obj, const QString& key)
{
    const QJsonValue entry = obj.value(key);
    if (!typeTagIs(entry, kTypeInt)) return std::nullopt;
    const QJsonValue v = entry.toObject().value(QLatin1String(kValueKey));
    const auto content = primitiveContent(v);
    if (!content) return std::nullopt;         // Kotlin intOrNull: null pass
    bool ok = false;
    const int parsed = content->toInt(&ok);    // "3" ok / "3.5" fail / "x" fail
    return ok ? std::optional<int>(parsed) : std::nullopt;
}

std::optional<float> SyncPreferenceJson::decodeFloat(
    const QJsonObject& obj, const QString& key)
{
    const QJsonValue entry = obj.value(key);
    if (!typeTagIs(entry, kTypeFloat)) return std::nullopt;
    const QJsonValue v = entry.toObject().value(QLatin1String(kValueKey));
    const auto content = primitiveContent(v);
    if (!content) return std::nullopt;
    bool ok = false;
    const float parsed = content->toFloat(&ok);
    return ok ? std::optional<float>(parsed) : std::nullopt;
}

std::optional<QSet<QString>> SyncPreferenceJson::decodeStringSet(
    const QJsonObject& obj, const QString& key)
{
    const QJsonValue entry = obj.value(key);
    if (!typeTagIs(entry, kTypeStringSet)) return std::nullopt;
    const QJsonValue v = entry.toObject().value(QLatin1String(kValueKey));
    if (!v.isArray()) return std::nullopt;

    // Kotlin: mapNotNull { as? JsonPrimitive }?.contentOrNull, trim(),
    // filter blanks, toSet() (dedup).
    QSet<QString> out;
    for (const auto& el : v.toArray()) {
        if (!el.isString() && !el.isDouble()) continue;
        const auto content = primitiveContent(el);
        if (!content) continue;
        const QString trimmed = content->trimmed();
        if (!trimmed.isEmpty()) out.insert(trimmed);
    }
    return out;
}

} // namespace nuvio::settings
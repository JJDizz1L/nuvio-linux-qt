#include "nuvio/updater/UpdateVersion.h"

#include <algorithm>

#include <QRegularExpression>

namespace nuvio::updater {

QString normalizeVersion(const QString& raw)
{
    QString out = raw.trimmed();
    if (out.startsWith(u'v') || out.startsWith(u'V')) out.remove(0, 1);
    return out;
}

QList<int> parseVersionParts(const QString& raw, bool* ok)
{
    const QString normalized = normalizeVersion(raw);
    if (normalized.isEmpty()) {
        if (ok) *ok = false;
        return {};
    }
    QList<int> parts;
    const QStringList tokens =
        normalized.split(QRegularExpression("[._\\-]"), Qt::SkipEmptyParts);
    for (const QString& token : tokens) {
        int digits = 0;
        while (digits < token.size() && token[digits].isDigit()) ++digits;
        bool conv = false;
        const int value = token.left(digits).toInt(&conv);
        if (conv) parts.append(value);
    }
    if (ok) *ok = !parts.isEmpty();
    return parts;
}

bool isRemoteNewer(const QString& remote, const QString& local)
{
    bool remoteOk = false, localOk = false;
    const QList<int> remoteParts = parseVersionParts(remote, &remoteOk);
    const QList<int> localParts = parseVersionParts(local, &localOk);
    if (!remoteOk || !localOk) {
        const QString remoteValue = normalizeVersion(remote);
        const QString localValue = normalizeVersion(local);
        return !remoteValue.isEmpty() && !localValue.isEmpty() &&
               remoteValue != localValue;
    }
    const int count = std::max(remoteParts.size(), localParts.size());
    for (int i = 0; i < count; ++i) {
        const int remoteValue = remoteParts.value(i, 0);
        const int localValue = localParts.value(i, 0);
        if (remoteValue != localValue) return remoteValue > localValue;
    }
    return false;
}

} // namespace nuvio::updater

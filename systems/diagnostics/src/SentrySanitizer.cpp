#include "nuvio/diagnostics/SentrySanitizer.h"

namespace nuvio::diagnostics {

QStringList sentryIgnoredIssueTexts()
{
    return {QStringLiteral("Large HTTP payload"),
            QStringLiteral("File IO on Main Thread")};
}

bool shouldDropSentryEvent(const QStringList& texts)
{
    const QStringList ignored = sentryIgnoredIssueTexts();
    for (const QString& text : texts) {
        for (const QString& fragment : ignored) {
            if (text.contains(fragment, Qt::CaseInsensitive)) return true;
        }
    }
    return false;
}

} // namespace nuvio::diagnostics

#pragma once

// Sentry event sanitizer (Appendix A, Sentry): verbatim port of Compose's
// SentryEventSanitizer (ignored issue texts + the drop predicate over the
// event's text surfaces). The envelope builder never emits request / user
// / serverName in the first place (sanitize() nulls them upstream), so
// only the drop decision needs porting.

#include <QStringList>

namespace nuvio::diagnostics {

[[nodiscard]] QStringList sentryIgnoredIssueTexts();

/// True when any text contains any ignored fragment (case-insensitive).
[[nodiscard]] bool shouldDropSentryEvent(const QStringList& texts);

} // namespace nuvio::diagnostics

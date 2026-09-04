#include "nuvio/diagnostics/SentrySettings.h"

#include <QProcessEnvironment>

#include "nuvio/diagnostics/SentryEnvelope.h"
#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::diagnostics {

namespace {

constexpr char kStoreName[] = "nuvio_sentry_settings";
constexpr char kEnabledKey[] = "enabled";
constexpr char kDsnEnv[] = "NUVIO_SENTRY_DSN";

} // namespace

bool SentrySettings::enabled() const
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(kStoreName));
    return store.getBoolean(kEnabledKey).value_or(true);
}

bool SentrySettings::supported() const
{
    const SentryDsn dsn = parseSentryDsn(
        QProcessEnvironment::systemEnvironment().value(
            QString::fromLatin1(kDsnEnv)));
    return dsn.valid();
}

void SentrySettings::setEnabled(bool on)
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(kStoreName));
    if (store.getBoolean(kEnabledKey).value_or(true) == on) return;
    store.putBoolean(kEnabledKey, on);
    emit enabledChanged();
}

} // namespace nuvio::diagnostics

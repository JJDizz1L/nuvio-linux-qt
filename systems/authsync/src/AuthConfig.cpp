#include "nuvio/authsync/AuthConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

namespace nuvio::authsync {
namespace {

/// java.util.Properties-lite reader: enough for local.properties lines of
/// form key=value / key:value with #-comments. No escape expansion needed
/// for the ASCII keys we look up.
bool readPropertiesFile(const QString& path,
                        QHash<QString, QString>* out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith('!'))
            continue;
        qsizetype eq = line.indexOf('=');
        if (eq < 0) eq = line.indexOf(':');
        if (eq <= 0) continue;
        out->insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    return true;
}

std::optional<QHash<QString, QString>> findLocalProperties()
{
    // Walk upward from the real executable location; stop after the repo
    // depth is exhausted. Portable: no absolute dev paths anywhere.
    const QString exeDir = QCoreApplication::instance()
        ? QCoreApplication::applicationDirPath()
        : QDir::currentPath();
    QDir dir(exeDir);
    for (int i = 0; i < 6 && dir.exists(); ++i) {
        const QString candidate = dir.filePath(QStringLiteral("local.properties"));
        if (QFileInfo::exists(candidate)) {
            QHash<QString, QString> props;
            if (readPropertiesFile(candidate, &props) &&
                props.contains(QStringLiteral("NUVIO_SUPABASE_URL")))
                return props;
        }
        if (!dir.cdUp()) break;
    }
    return std::nullopt;
}

} // namespace

QByteArray AuthConfig::tokenUrl() const
{ return baseUrl + "/auth/v1/token?grant_type=password"; }
QByteArray AuthConfig::signupUrl() const
{ return baseUrl + "/auth/v1/signup"; }
QByteArray AuthConfig::refreshUrl() const
{ return baseUrl + "/auth/v1/token?grant_type=refresh_token"; }
QByteArray AuthConfig::userUrl() const
{ return baseUrl + "/auth/v1/user"; }
QByteArray AuthConfig::rpcUrl(const char* fn) const
{
    return baseUrl + "/rest/v1/rpc/"
           + (fn ? QByteArray(fn) : QByteArray());
}

AuthConfig AuthConfig::load()
{
    AuthConfig c;
    c.baseUrl  = qgetenv("NUVIO_SUPABASE_URL");
    c.anonKey  = qgetenv("NUVIO_SUPABASE_ANON_KEY");
    if (!c.anonKey.isEmpty() && !c.baseUrl.isEmpty()) return c;

    if (auto props = findLocalProperties()) {
        const auto get = [&](const char* k, QByteArray& into) {
            if (into.isEmpty())
                into = props->value(QLatin1String(k)).toUtf8();
        };
        get("NUVIO_SUPABASE_URL", c.baseUrl);
        get("NUVIO_SUPABASE_ANON_KEY", c.anonKey);
    }
    return c;
}

} // namespace nuvio::authsync
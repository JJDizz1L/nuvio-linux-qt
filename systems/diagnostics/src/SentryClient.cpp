#include "nuvio/diagnostics/SentryClient.h"

#include <csignal>
#include <cstdio>
#include <exception>
#include <typeinfo>

#include <fcntl.h>
#include <unistd.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QSysInfo>

#include "nuvio/diagnostics/SentryEnvelope.h"
#include "nuvio/diagnostics/SentryMetadata.h"
#include "nuvio/diagnostics/SentrySettings.h"
#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::diagnostics {

namespace {

constexpr char kDsnEnv[] = "NUVIO_SENTRY_DSN";
constexpr char kEnvEnv[] = "NUVIO_SENTRY_ENVIRONMENT";
constexpr char kClientId[] = "nuvio-qt/0.1";
constexpr int kMaxPendingFiles = 5;

// Async-signal-safe pending-dir slot (written once at start(), read only
// inside the fatal-signal handler).
char g_pendingDir[4096] = {};

void appendUint(char*& p, const char* end, unsigned long value)
{
    char digits[32];
    int len = 0;
    do {
        digits[len++] = char('0' + value % 10);
        value /= 10;
    } while (value > 0 && len < 31);
    while (len > 0 && p < end) {
        *p++ = digits[--len];
    }
}

void writeCrashMarker(int sig)
{
    if (g_pendingDir[0] == '\0') return;
    char path[4352];
    char* p = path;
    const char* end = path + sizeof(path) - 1;
    for (const char* s = g_pendingDir; *s && p < end; ++s) *p++ = *s;
    const char suffix[] = "/pending/";
    for (const char* s = suffix; *s && p < end; ++s) *p++ = *s;
    appendUint(p, end, static_cast<unsigned long>(::getpid()));
    const char ext[] = ".signal";
    for (const char* s = ext; *s && p < end; ++s) *p++ = *s;
    *p = '\0';
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    char body[64];
    char* b = body;
    const char* bend = body + sizeof(body) - 1;
    const char prefix[] = "signal=";
    for (const char* s = prefix; *s && b < bend; ++s) *b++ = *s;
    appendUint(b, bend, static_cast<unsigned long>(sig));
    if (b < bend) *b++ = '\n';
    ::write(fd, body, static_cast<size_t>(b - body));
    ::close(fd);
}

void fatalSignalHandler(int sig)
{
    writeCrashMarker(sig);
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

std::terminate_handler g_previousTerminate = nullptr;

void terminateHandler()
{
    QString detail = QStringLiteral("unknown");
    try {
        const std::exception_ptr eptr = std::current_exception();
        if (eptr) {
            try {
                std::rethrow_exception(eptr);
            } catch (const std::exception& e) {
                detail = QString::fromLatin1(typeid(e).name()) +
                         QStringLiteral(": ") +
                         QString::fromUtf8(e.what());
            } catch (...) {
                detail = QStringLiteral("non-std exception");
            }
        }
    } catch (...) {
    }
    QDir dir;
    dir.mkpath(QString::fromUtf8(g_pendingDir) +
               QStringLiteral("/pending"));
    QFile marker(QString::fromUtf8(g_pendingDir) +
                 QStringLiteral("/pending/terminate.pending"));
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write(QJsonDocument(QJsonObject{
                                       {QStringLiteral("kind"),
                                        QStringLiteral("terminate")},
                                       {QStringLiteral("detail"), detail},
                                   })
                         .toJson(QJsonDocument::Compact));
        marker.close();
    }
    if (g_previousTerminate) {
        g_previousTerminate();
        ::abort();
    }
    ::abort();
}

QString sentryCacheDir()
{
    const QString home = QDir::homePath();
    const QString base = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("XDG_CACHE_HOME"),
        home + QStringLiteral("/.cache"));
    return base + QStringLiteral("/nuvio/sentry");   // cacheDir parity
}

QString signalName(int sig)
{
    switch (sig) {
    case SIGSEGV:
        return QStringLiteral("SIGSEGV");
    case SIGABRT:
        return QStringLiteral("SIGABRT");
    case SIGILL:
        return QStringLiteral("SIGILL");
    case SIGFPE:
        return QStringLiteral("SIGFPE");
    case SIGBUS:
        return QStringLiteral("SIGBUS");
    default:
        return QStringLiteral("signal %1").arg(sig);
    }
}

} // namespace

SentryClient::SentryClient(QString versionName, QString environment,
                           bool installHooks, QObject* parent)
    : QObject(parent),
      m_versionName(std::move(versionName)),
      m_environment(environment.isEmpty()
                        ? QProcessEnvironment::systemEnvironment().value(
                              QString::fromLatin1(kEnvEnv),
                              QStringLiteral("production"))
                        : environment),
      m_installHooks(installHooks),
      m_settings(new SentrySettings(this)),
      m_nam(new QNetworkAccessManager(this))
{
    connect(m_settings, &SentrySettings::enabledChanged, this,
            &SentryClient::enabledChanged);
    connect(m_settings, &SentrySettings::enabledChanged, this,
            [this] { applyEnabled(m_settings->enabled()); });
}

SentryClient::~SentryClient() = default;

bool SentryClient::enabled() const { return m_settings->enabled(); }

bool SentryClient::supported() const
{
    return parseSentryDsn(dsnString()).valid();
}

void SentryClient::setEnabled(bool on) { m_settings->setEnabled(on); }

void SentryClient::start()
{
    if (m_started) return;   // initializer start() parity
    m_started = true;
    if (!supported()) {
        setActive(false);
        return;
    }
    if (m_installHooks) {
        const QByteArray dir = QFile::encodeName(sentryCacheDir());
        QDir().mkpath(sentryCacheDir() + QStringLiteral("/pending"));
        ::snprintf(g_pendingDir, sizeof(g_pendingDir), "%s",
                   dir.constData());
        g_previousTerminate = std::set_terminate(terminateHandler);
        ::signal(SIGSEGV, fatalSignalHandler);
        ::signal(SIGABRT, fatalSignalHandler);
        ::signal(SIGILL, fatalSignalHandler);
        ::signal(SIGFPE, fatalSignalHandler);
        ::signal(SIGBUS, fatalSignalHandler);
    }
    addLifecycleCrumb();
    applyEnabled(m_settings->enabled());
}

void SentryClient::captureMessage(const QString& text,
                                  const QString& level)
{
    if (!m_active || text.trimmed().isEmpty()) return;
    sendEvent(text, {}, {}, level.isEmpty() ? QStringLiteral("error")
                                            : level);
}

void SentryClient::applyEnabled(bool on)
{
    if (!m_started || !supported()) {
        setActive(false);
        return;
    }
    setActive(on);
    if (on) flushPendingCrashes();
}

void SentryClient::setActive(bool on)
{
    if (m_active == on) return;
    m_active = on;
    emit activeChanged();
}

void SentryClient::addLifecycleCrumb()
{
    m_crumbTrail.append(QStringLiteral("app.lifecycle: Application started"));
}

void SentryClient::sendEvent(const QString& message,
                             const QString& exceptionType,
                             const QString& exceptionValue,
                             const QString& level)
{
    const SentryDsn dsn = parseSentryDsn(dsnString());
    if (!dsn.valid()) return;
    const SentryHostMetadata meta = currentSentryMetadata(m_versionName);
    SentryEvent event;
    event.level = level;
    event.logger = QStringLiteral("nuvio.qt");
    event.message = message;
    event.exceptionType = exceptionType;
    event.exceptionValue = exceptionValue;
    event.tags = QJsonObject{
        {QStringLiteral("app.package_name"),
         QString::fromLatin1(kSentryPackage)},
        {QStringLiteral("app.version_name"), m_versionName},
        {QStringLiteral("app.version_code"),
         versionCodeFromString(m_versionName)},
        {QStringLiteral("desktop.platform"), meta.platform},
        {QStringLiteral("desktop.architecture"), meta.architecture},
    };
    event.release = meta.release;
    event.dist = meta.distribution;
    event.environment = m_environment;
    const QString now =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const QString& crumb : m_crumbTrail) {
        const int cut = crumb.indexOf(": ");
        event.breadcrumbs.append(
            {now, cut > 0 ? crumb.left(cut) : QStringLiteral("app"),
             cut > 0 ? crumb.mid(cut + 2) : crumb});
    }
    const QJsonObject json = buildSentryEventJson(event);
    if (json.isEmpty()) return;   // sanitizer drop
    postEnvelope(buildSentryEnvelope(json));
}

void SentryClient::postEnvelope(const QByteArray& envelope)
{
    if (envelope.isEmpty()) return;
    const SentryDsn dsn = parseSentryDsn(dsnString());
    if (!dsn.valid()) return;
    QNetworkRequest req{QUrl(sentryEnvelopeUrl(dsn))};
    req.setRawHeader("X-Sentry-Auth",
                     sentryAuthHeader(dsn, QString::fromLatin1(kClientId))
                         .toLatin1());
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-sentry-envelope");
    QNetworkReply* rep = m_nam->post(req, envelope);
    connect(rep, &QNetworkReply::finished, rep,
            &QNetworkReply::deleteLater);
}

void SentryClient::flushPendingCrashes()
{
    const QDir pending(sentryCacheDir() + QStringLiteral("/pending"));
    if (!pending.exists()) return;
    QStringList files =
        pending.entryList({"*.signal", "*.pending"}, QDir::Files,
                          QDir::Time | QDir::Reversed);
    while (files.size() > kMaxPendingFiles) {
        QFile::remove(pending.filePath(files.takeLast()));
    }
    for (const QString& file : files) {
        const QString path = pending.filePath(file);
        QString exceptionType, exceptionValue;
        if (file.endsWith(QStringLiteral(".signal"))) {
            QFile marker(path);
            int sig = 0;
            if (marker.open(QIODevice::ReadOnly)) {
                const QByteArray body = marker.readAll().trimmed();
                marker.close();
                if (body.startsWith("signal="))
                    sig = body.mid(7).toInt();
            }
            exceptionType = signalName(sig);
            exceptionValue =
                QStringLiteral("Native crash in the previous session");
        } else {
            QFile marker(path);
            QByteArray body;
            if (marker.open(QIODevice::ReadOnly)) {
                body = marker.readAll();
                marker.close();
            }
            const QJsonObject obj =
                QJsonDocument::fromJson(body).object();
            if (obj.value(QStringLiteral("kind")).toString() !=
                QStringLiteral("terminate"))
                continue;
            exceptionType = QStringLiteral("terminate");
            exceptionValue =
                obj.value(QStringLiteral("detail")).toString();
        }
        QFile::remove(path);
        sendEvent({}, exceptionType, exceptionValue,
                  QStringLiteral("fatal"));
    }
}

QString SentryClient::dsnString() const
{
    return QProcessEnvironment::systemEnvironment()
        .value(QString::fromLatin1(kDsnEnv))
        .trimmed();
}

} // namespace nuvio::diagnostics

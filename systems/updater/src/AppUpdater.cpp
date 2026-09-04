#include "nuvio/updater/AppUpdater.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/updater/AppUpdate.h"
#include "nuvio/updater/UpdateVersion.h"

namespace nuvio::updater {

namespace {

QString releasesFeedUrl(const QString& apiBase)
{
    return apiBase + QStringLiteral("/repos/") +
           QString::fromLatin1(kUpdateOwner) + QLatin1Char('/') +
           QString::fromLatin1(kUpdateRepo) +
           QStringLiteral("/releases?per_page=20");
}

QString sanitizeAssetName(const QString& name)
{
    QString out = name;
    out.replace(QRegularExpression("[^a-zA-Z0-9._-]"), "_");
    return out.isEmpty() ? QStringLiteral("update.bin") : out;
}

} // namespace

AppUpdater::AppUpdater(QString currentVersion, QString apiBaseOverride,
                       QObject* parent)
    : QObject(parent),
      m_currentVersion(std::move(currentVersion)),
      m_apiBase(apiBaseOverride.isEmpty()
                    ? QString::fromLatin1(kGitHubApiBase)
                    : apiBaseOverride),
      m_nam(new QNetworkAccessManager(this))
{}

void AppUpdater::ensureAutoCheckStarted()
{
    if (m_autoStarted) return;   // desktop: enabled + supported always
    m_autoStarted = true;
    checkForUpdates(false, false);
}

void AppUpdater::checkForUpdates(bool force, bool showNoUpdateFeedback)
{
    if (m_checking) return;
    if (m_checkReply) {
        m_checkReply->abort();
        m_checkReply->deleteLater();
        m_checkReply = nullptr;
    }
    setChecking(true);
    setError({});
    setShowDialog(false);
    QNetworkRequest req{QUrl(releasesFeedUrl(m_apiBase))};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent",
                     QByteArray(kUpdateUserAgent));
    m_checkReply = m_nam->get(req);
    connect(m_checkReply, &QNetworkReply::finished, this,
            [this, force, showNoUpdateFeedback] {
                QNetworkReply* rep = m_checkReply;
                m_checkReply = nullptr;
                rep->deleteLater();
                setChecking(false);
                if (rep->error() != QNetworkReply::NoError) {
                    applyCheckFailure(
                        tr("Update check failed (HTTP %1)")
                            .arg(rep->attribute(
                                         QNetworkRequest::
                                             HttpStatusCodeAttribute)
                                     .toInt()),
                        force, showNoUpdateFeedback);
                    return;
                }
                const LatestUpdateResult parsed = parseLatestUpdate(
                    rep->readAll(), true /* includePrereleases */);
                if (parsed.malformed) {
                    applyCheckFailure(tr("Update check failed: the "
                                         "latest release has no "
                                         "installable file"),
                                      force, showNoUpdateFeedback);
                    return;
                }
                if (!parsed.update) {
                    // NoChannelRelease parity: silent unless forced with
                    // feedback (then it is just "latest").
                    if (showNoUpdateFeedback)
                        emit notice(tr("You're on the latest version"));
                    if (force) setShowDialog(false);
                    return;
                }
                const AppUpdate& update = *parsed.update;
                applyCheckSuccess(update.tag, update.title, update.notes,
                                  update.releaseUrl, update.assetName,
                                  update.assetUrl, update.assetSizeBytes,
                                  force);
                if (showNoUpdateFeedback && !m_available)
                    emit notice(tr("You're on the latest version"));
            });
}

void AppUpdater::applyCheckSuccess(const QString& tag, const QString& title,
                                   const QString& notes,
                                   const QString& releaseUrl,
                                   const QString& assetName,
                                   const QString& assetUrl,
                                   long long assetSize, bool force)
{
    const bool remoteNewer = isRemoteNewer(tag, m_currentVersion);
    const QString ignored = ignoredTag();
    const bool isIgnored = !ignored.isEmpty() && ignored == tag;
    m_available = remoteNewer;
    if (remoteNewer) {
        m_tag = tag;
        m_title = title;
        m_notes = notes;
        m_releaseUrl = releaseUrl;
        m_assetName = assetName;
        m_assetUrl = assetUrl;
        m_assetSize = assetSize;
    } else {
        m_tag.clear();
        m_title.clear();
        m_notes.clear();
        m_releaseUrl.clear();
        m_assetName.clear();
        m_assetUrl.clear();
        m_assetSize = -1;
        m_downloadedPath.clear();
        emit downloadedPathChanged();
    }
    setDownloading(false);
    setProgress(-1);
    setShowDialog(force || (remoteNewer && !isIgnored));
    setError({});
    emit updateAvailableChanged();
    emit updateChanged();
}

void AppUpdater::applyCheckFailure(const QString& message, bool force,
                                   bool showNoUpdateFeedback)
{
    setDownloading(false);
    setProgress(-1);
    m_downloadedPath.clear();
    emit downloadedPathChanged();
    m_available = false;
    m_tag.clear();
    emit updateAvailableChanged();
    emit updateChanged();
    // showDialog + error only on forced checks (controller parity).
    setShowDialog(force);
    setError(force ? message : QString{});
    if (showNoUpdateFeedback) emit notice(message);
}

void AppUpdater::dismissBanner()
{
    setShowDialog(false);
    setError({});
}

void AppUpdater::ignoreThisVersion()
{
    if (m_tag.isEmpty()) return;
    setIgnoredTag(m_tag);
    dismissBanner();
}

void AppUpdater::downloadUpdate()
{
    if (m_tag.isEmpty() || m_assetUrl.isEmpty() || m_downloading) return;
    if (m_downloadReply) {
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }
    delete m_partFile;
    m_partFile = nullptr;
    setDownloading(true);
    setProgress(0);
    setError({});
    m_downloadedBytes = 0;
    m_totalBytes = -1;
    QNetworkRequest req{QUrl(m_assetUrl)};
    req.setRawHeader("User-Agent", QByteArray(kUpdateUserAgent));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    m_downloadReply = m_nam->get(req);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                m_downloadedBytes = received;
                m_totalBytes = total;
                setProgress(total > 0 ? std::clamp(double(received) /
                                                       double(total),
                                                   0.0, 1.0)
                                      : -1);
            });
    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this] {
        if (!m_partFile) {
            QDir dir(updatesDirPath());
            dir.removeRecursively();   // clearDir parity
            dir.mkpath(QStringLiteral("."));
            m_destPath = dir.filePath(sanitizeAssetName(m_assetName));
            m_partPath = m_destPath + QStringLiteral(".part");
            m_partFile = new QFile(m_partPath, this);
            if (!m_partFile->open(QIODevice::WriteOnly)) {
                failDownload(tr("Couldn't write the update file"));
                return;
            }
        }
        m_partFile->write(m_downloadReply->readAll());
    });
    connect(m_downloadReply, &QNetworkReply::finished, this,
            [this] { finishDownload(); });
}

void AppUpdater::finishDownload()
{
    QNetworkReply* rep = m_downloadReply;
    m_downloadReply = nullptr;
    if (rep) {
        if (m_partFile) m_partFile->write(rep->readAll());
        const int status =
            rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool httpOk = status >= 200 && status <= 299;
        const QNetworkReply::NetworkError err = rep->error();
        rep->deleteLater();
        if (err != QNetworkReply::NoError || !httpOk) {
            failDownload(tr("Download failed (HTTP %1)").arg(status));
            return;
        }
        // Content-Length mismatch guard (desktop parity).
        const long long expected =
            rep->header(QNetworkRequest::ContentLengthHeader)
                .toLongLong();
        if (expected > 0 && m_partFile &&
            m_partFile->size() != expected) {
            failDownload(tr("Download failed: incomplete file"));
            return;
        }
    }
    if (m_partFile) {
        m_partFile->flush();
        m_partFile->close();
            if (!QFile::rename(m_partPath, m_destPath)) {
                QFile::remove(m_destPath);
                if (!QFile::rename(m_partPath, m_destPath)) {
                    if (!QFile::copy(m_partPath, m_destPath)) {
                    failDownload(tr("Couldn't write the update file"));
                    return;
                }
                QFile::remove(m_partPath);
            }
        }
        m_partFile->deleteLater();
        m_partFile = nullptr;
    }
    if (m_destPath.isEmpty() || !QFileInfo::exists(m_destPath)) {
        failDownload(tr("Download failed: empty file"));
        return;
    }
    setDownloading(false);
    setProgress(1);
    m_downloadedPath = m_destPath;
    emit downloadedPathChanged();
    setError({});
    installDownloadedUpdate();
}

void AppUpdater::failDownload(const QString& message)
{
    if (m_downloadReply) {
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }
    if (m_partFile) {
        const QString part = m_partPath;
        m_partFile->close();
        m_partFile->deleteLater();
        m_partFile = nullptr;
        QFile::remove(part);
    }
    setDownloading(false);
    setProgress(-1);
    m_downloadedPath.clear();
    emit downloadedPathChanged();
    setError(message);
    setShowDialog(true);
}

void AppUpdater::installDownloadedUpdate()
{
    if (m_downloadedPath.isEmpty()) return;
    if (!QFileInfo::exists(m_downloadedPath)) {
        setError(tr("Downloaded update file is missing"));
        setShowDialog(true);
        return;
    }
    // Desktop Linux parity: hand the package to the OS, then exit. The
    // child gets null stdio: an inheriting installer descendant must never
    // hold OUR stdout pipe open (it stalls harness log drains forever).
    QProcess installer;
    installer.setProgram(m_installer.isEmpty() ? QStringLiteral("xdg-open")
                                               : m_installer);
    installer.setArguments({m_downloadedPath});
    installer.setStandardInputFile(QProcess::nullDevice());
    installer.setStandardOutputFile(QProcess::nullDevice());
    installer.setStandardErrorFile(QProcess::nullDevice());
    qint64 installerPid = 0;
    if (!installer.startDetached(&installerPid)) {
        setError(tr("Couldn't open the update file"));
        setShowDialog(true);
        return;
    }
    QTimer::singleShot(500, qApp, &QCoreApplication::quit);
}

void AppUpdater::setChecking(bool on)
{
    if (m_checking == on) return;
    m_checking = on;
    emit checkingChanged();
}

void AppUpdater::setDownloading(bool on)
{
    if (m_downloading == on) return;
    m_downloading = on;
    emit downloadingChanged();
}

void AppUpdater::setProgress(double value)
{
    if (m_progress == value) return;
    m_progress = value;
    emit downloadProgressChanged();
}

void AppUpdater::setError(const QString& message)
{
    if (m_error == message) return;
    m_error = message;
    emit errorMessageChanged();
}

void AppUpdater::setShowDialog(bool on)
{
    const bool before = bannerVisible();
    m_showDialog = on;
    if (bannerVisible() != before) emit bannerVisibleChanged();
}

QString AppUpdater::ignoredTag()
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("nuvio_updater"));
    const auto raw = store.getString("ignored_release_tag");
    if (!raw || raw->empty()) return {};
    return QString::fromStdString(*raw);
}

void AppUpdater::setIgnoredTag(const QString& tag)
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("nuvio_updater"));
    store.putString("ignored_release_tag", tag.toStdString());
}

QString AppUpdater::updatesDirPath()
{
    const QFileInfo anchor(QString::fromStdString(
        nuvio::settings::PropertiesStore::defaultPath("nuvio_updater")
            .string()));
    return anchor.absolutePath() + QStringLiteral("/updates");
}

} // namespace nuvio::updater

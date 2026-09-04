#pragma once

// In-app updater controller (Appendix A, updater): Qt port of Compose's
// AppUpdaterController + desktop AppUpdaterPlatform actual. Checks the
// product releases feed, compares against the build version, downloads
// the best Linux asset with progress, and hands it to xdg-open (then
// exits, desktop parity). Async throughout - never blocks the QML thread.
//
// Linux install path is xdg-open (a .deb opens in the package manager,
// an .AppImage launches); canInstall is unconditionally true and the
// unknown-sources permission dialog is a desktop/Android-only concern.

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QObject>
#include <QString>

namespace nuvio::updater {

class AppUpdater final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY
                   updateAvailableChanged)
    Q_PROPERTY(QString updateTag READ updateTag NOTIFY updateChanged)
    Q_PROPERTY(QString updateTitle READ updateTitle NOTIFY updateChanged)
    Q_PROPERTY(QString updateNotes READ updateNotes NOTIFY updateChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY updateChanged)
    Q_PROPERTY(QString assetName READ assetName NOTIFY updateChanged)
    Q_PROPERTY(long long assetSize READ assetSize NOTIFY updateChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged)
    // 0..1 download fraction, -1 while the size is unknown (fork null).
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY
                   downloadProgressChanged)
    Q_PROPERTY(QString downloadedPath READ downloadedPath NOTIFY
                   downloadedPathChanged)
    // Fork's showDialog && update != null (banner visibility).
    Q_PROPERTY(bool bannerVisible READ bannerVisible NOTIFY
                   bannerVisibleChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY
                   errorMessageChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

public:
    /// currentVersion is the build version (NUVIO_VERSION_STRING);
    /// apiBaseOverride reroutes the releases feed in tests (default is
    /// the GitHub API product feed).
    explicit AppUpdater(QString currentVersion,
                        QString apiBaseOverride = {},
                        QObject* parent = nullptr);

    [[nodiscard]] bool checking() const { return m_checking; }
    [[nodiscard]] bool updateAvailable() const { return m_available; }
    [[nodiscard]] QString updateTag() const { return m_tag; }
    [[nodiscard]] QString updateTitle() const { return m_title; }
    [[nodiscard]] QString updateNotes() const { return m_notes; }
    [[nodiscard]] QString releaseUrl() const { return m_releaseUrl; }
    [[nodiscard]] QString assetName() const { return m_assetName; }
    [[nodiscard]] long long assetSize() const { return m_assetSize; }
    [[nodiscard]] bool downloading() const { return m_downloading; }
    [[nodiscard]] double downloadProgress() const { return m_progress; }
    [[nodiscard]] QString downloadedPath() const { return m_downloadedPath; }
    [[nodiscard]] bool bannerVisible() const
    {
        return m_showDialog && !m_tag.isEmpty() && m_available;
    }
    [[nodiscard]] QString errorMessage() const { return m_error; }
    [[nodiscard]] QString appVersion() const { return m_currentVersion; }

    /// Test seam: replaces the "xdg-open" installer launcher (suite sets
    /// "/bin/true" so the download path never opens a real handler).
    void setInstallerOverride(const QString& program)
    {
        m_installer = program;
    }

    /// Starts the silent background check once (MainAppContent parity).
    Q_INVOKABLE void ensureAutoCheckStarted();    Q_INVOKABLE void checkForUpdates(bool force, bool showNoUpdateFeedback);
    Q_INVOKABLE void dismissBanner();
    Q_INVOKABLE void ignoreThisVersion();
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void installDownloadedUpdate();

signals:
    void checkingChanged();
    void updateAvailableChanged();
    void updateChanged();
    void downloadingChanged();
    void downloadProgressChanged();
    void downloadedPathChanged();
    void bannerVisibleChanged();
    void errorMessageChanged();
    /// Transient feedback (latest-version / failure toasts).
    void notice(const QString& message);

private:
    void setChecking(bool on);
    void setDownloading(bool on);
    void setProgress(double value);
    void setError(const QString& message);
    void setShowDialog(bool on);
    void applyCheckSuccess(const QString& tag, const QString& title,
                           const QString& notes, const QString& releaseUrl,
                           const QString& assetName, const QString& assetUrl,
                           long long assetSize, bool force);
    void applyCheckFailure(const QString& message, bool force,
                           bool showNoUpdateFeedback);
    void finishDownload();
    void failDownload(const QString& message);
    [[nodiscard]] static QString ignoredTag();
    static void setIgnoredTag(const QString& tag);
    [[nodiscard]] static QString updatesDirPath();

    QString m_currentVersion;
    QString m_apiBase;
    QString m_installer = QStringLiteral("xdg-open");
    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_checkReply = nullptr;
    QNetworkReply* m_downloadReply = nullptr;

    bool m_autoStarted = false;
    bool m_checking = false;
    bool m_available = false;
    QString m_tag, m_title, m_notes, m_releaseUrl;
    QString m_assetName, m_assetUrl;
    long long m_assetSize = -1;
    bool m_downloading = false;
    double m_progress = -1;
    QString m_downloadedPath;
    bool m_showDialog = false;
    QString m_error;

    // Streaming download state.
    QFile* m_partFile = nullptr;
    QString m_partPath;
    QString m_destPath;
    long long m_downloadedBytes = 0;
    long long m_totalBytes = -1;
};

} // namespace nuvio::updater

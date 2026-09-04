// OFFLINE updater tests (version compare, asset selection, release
// parsing, controller check/download against a local HTTP fake).
// ISOLATION: XDG sandbox. The install path (xdg-open + quit) is NOT
// exercised - it would leave the test process by design.
#include <nuvio/updater/AppUpdate.h>
#include <nuvio/updater/AppUpdater.h>
#include <nuvio/updater/UpdateAssets.h>
#include <nuvio/updater/UpdateVersion.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

using nuvio::updater::AppUpdater;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/// Minimal HTTP fake: routes full request paths to canned bodies.
class FakeHttp final : public QObject {
public:
    QHash<QByteArray, QByteArray> routes;   // path suffix -> body
    QHash<QByteArray, QByteArray> seen;     // path suffix -> last body
    QList<QByteArray> paths;

    bool start()
    {
        if (!m_srv.listen(QHostAddress::LocalHost)) return false;
        m_port = m_srv.serverPort();
        connect(&m_srv, &QTcpServer::newConnection, this,
                [this] { accept(); });
        return true;
    }
    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_port);
    }

private:
    void accept()
    {
        auto* sock = m_srv.nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
            m_buf[sock] += sock->readAll();
            const int hdrEnd = m_buf[sock].indexOf("\r\n\r\n");
            if (hdrEnd < 0) return;
            const QByteArray head = m_buf[sock].left(hdrEnd);
            const QByteArray path =
                head.split('\n').value(0).split(' ').value(1);
            paths << path;
            QByteArray payload = "{}";
            QByteArray matched;
            for (auto it = routes.constBegin(); it != routes.constEnd();
                 ++it) {
                if (path.endsWith(it.key())) {
                    payload = it.value();
                    matched = it.key();
                    break;
                }
            }
            if (!matched.isEmpty()) seen.insert(matched, payload);
            QByteArray out = "HTTP/1.1 200 OK\r\nContent-Type: "
                             "application/octet-stream\r\nConnection: "
                             "close\r\nContent-Length: ";
            out += QByteArray::number(payload.size());
            out += "\r\n\r\n" + payload;
            sock->write(out);
            sock->flush();
            sock->disconnectFromHost();
            m_buf.remove(sock);
        });
    }

    QTcpServer m_srv;
    QHash<QTcpSocket*, QByteArray> m_buf;
    quint16 m_port = 0;
};

QByteArray releaseJson(const QByteArray& tag, bool prerelease,
                       const QByteArray& assetName, const QByteArray& url)
{
    return "[{\"tag_name\":\"" + tag + "\",\"name\":\"Nuvio " + tag +
           "\",\"body\":\"Notes here\",\"draft\":false,\"prerelease\":" +
           (prerelease ? "true" : "false") +
           ",\"html_url\":\"https://example/r\",\"assets\":[{\"name\":\"" +
           assetName + "\",\"browser_download_url\":\"" + url +
           "\",\"size\":11,\"content_type\":"
           "\"application/octet-stream\"}]}]";
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME",
            QDir(sandbox.path()).filePath("cfg").toUtf8());
    QDir().mkpath(QString::fromUtf8(qgetenv("XDG_CONFIG_HOME")));

    { // T1: version compare (VersionUtils parity).
        using nuvio::updater::isRemoteNewer;
        CHECK(isRemoteNewer("v1.2.4", "1.2.3"), "patch newer");
        CHECK(!isRemoteNewer("1.2.3", "v1.2.4"), "patch older");
        CHECK(!isRemoteNewer("1.2", "1.2.0"), "equal across lengths");
        CHECK(isRemoteNewer("1.10", "1.9"), "numeric not lexical");
        CHECK(isRemoteNewer("1.0.0", "0.1.20.0"), "major beats ours");
        CHECK(!isRemoteNewer("0.1.20.0", "0.1.20.0"), "equal silent");
        CHECK(isRemoteNewer("nightly", "stable"), "unparseable differ");
        CHECK(!isRemoteNewer("", "1.0"), "blank remote never newer");
    }

    { // T2: asset selection (desktop Linux parity).
        using nuvio::updater::UpdateAssetCandidate;
        const auto sel =
            nuvio::updater::linuxAssetSelector(QStringLiteral("amd64"));
        CHECK(sel.fileExtensions.contains(".deb") &&
                  sel.fileExtensions.contains(".AppImage"),
              "linux extensions");
        const QList<UpdateAssetCandidate> assets = {
            {"nuvio-linux.AppImage", "u1", 1, {}},
            {"nuvio-x86_64.deb", "u2", 2, {}},
            {"nuvio-win.exe", "u3", 3, {}},
        };
        const auto* best =
            nuvio::updater::selectBestUpdateAsset(assets, sel);
        CHECK(best && best->downloadUrl == "u2", "arch deb wins");
        const QList<UpdateAssetCandidate> none = {
            {"nuvio-win.exe", "u3", 3, {}},
        };
        CHECK(nuvio::updater::selectBestUpdateAsset(none, sel) == nullptr,
              "no match is null");
        const QList<UpdateAssetCandidate> universal = {
            {"nuvio-linux.AppImage", "u1", 1, {}},
            {"nuvio-universal.deb", "u4", 4, {}},
        };
        const auto* fb =
            nuvio::updater::selectBestUpdateAsset(universal, sel);
        CHECK(fb && fb->downloadUrl == "u1", "plain linux beats fallback");
    }

    { // T3: release parsing (repository parity).
        using nuvio::updater::parseLatestUpdate;
        const QByteArray body = releaseJson("v9.9", false, "n-x86_64.deb",
                                            "https://example/d");
        const auto ok = parseLatestUpdate(body, true);
        CHECK(ok.update && ok.update->tag == "v9.9" &&
                  ok.update->assetName == "n-x86_64.deb" &&
                  ok.update->assetSizeBytes == 11 && !ok.malformed,
              "release parsed");
        const auto preExcluded = parseLatestUpdate(
            releaseJson("v9.9", true, "n-x86_64.deb", "https://example/d"),
            false);
        CHECK(!preExcluded.update && !preExcluded.malformed,
              "prerelease gated");
        const auto preIncluded = parseLatestUpdate(
            releaseJson("v9.9", true, "n-x86_64.deb", "https://example/d"),
            true);
        CHECK(preIncluded.update, "prerelease included when asked");
        const auto noAsset = parseLatestUpdate(
            releaseJson("v9.9", false, "n-win.exe", "https://example/d"),
            true);
        CHECK(!noAsset.update && noAsset.malformed, "no asset malformed");
        const auto empty = parseLatestUpdate("[]", true);
        CHECK(!empty.update && !empty.malformed, "empty is silent");
        const auto garbage = parseLatestUpdate("not json", true);
        CHECK(!garbage.update && !garbage.malformed, "garbage is silent");
    }

    FakeHttp http;
    if (!http.start()) return 2;

    { // T4: forced check surfaces the banner; ignore silences it.
        http.routes.insert(
            "/releases?per_page=20",
            releaseJson("v9.9", true, "n-x86_64.deb",
                        (http.baseUrl() + "/n-x86_64.deb").toUtf8()));
        AppUpdater updater(QStringLiteral("0.1.20.0"), http.baseUrl());
        int notices = 0;
        QObject::connect(&updater, &AppUpdater::notice,
                         [&](const QString&) { ++notices; });
        updater.checkForUpdates(true, false);
        pump(300);
        CHECK(updater.updateAvailable() &&
                  updater.updateTag() == "v9.9" && updater.bannerVisible() &&
                  updater.assetName() == "n-x86_64.deb",
              "banner on forced newer check");
        updater.ignoreThisVersion();
        CHECK(!updater.bannerVisible(), "ignore dismisses");
        updater.checkForUpdates(false, false);   // auto re-check
        pump(300);
        CHECK(updater.updateAvailable() && !updater.bannerVisible(),
              "ignored tag stays silent");
        CHECK(notices == 0, "no feedback noise");
    }

    { // T5: up-to-date check toasts "latest" only when asked.
        http.routes.insert(
            "/releases?per_page=20",
            releaseJson("v0.1.10", false, "n-x86_64.deb",
                        (http.baseUrl() + "/n-x86_64.deb").toUtf8()));
        AppUpdater updater(QStringLiteral("0.1.20.0"), http.baseUrl());
        int notices = 0;
        QObject::connect(&updater, &AppUpdater::notice,
                         [&](const QString&) { ++notices; });
        updater.checkForUpdates(true, true);
        pump(300);
        CHECK(!updater.updateAvailable() && !updater.bannerVisible() &&
                  notices == 1,
              "forced current toasts latest");
    }

    { // T6: download streams the asset to the updates dir.
        http.routes.insert("/n-x86_64.deb", QByteArray("0123456789A"));
        http.routes.insert(
            "/releases?per_page=20",
            releaseJson("v9.9", true, "n-x86_64.deb",
                        (http.baseUrl() + "/n-x86_64.deb").toUtf8()));
        AppUpdater updater(QStringLiteral("0.1.20.0"), http.baseUrl());
        updater.setInstallerOverride(QStringLiteral("/bin/true"));
        updater.checkForUpdates(true, false);
        pump(300);
        CHECK(updater.updateAvailable(), "update staged");
        updater.downloadUpdate();
        pump(400);
        CHECK(!updater.downloading() &&
                  updater.downloadProgress() == 1.0 &&
                  QFile::exists(updater.downloadedPath()) &&
                  updater.downloadedPath().endsWith("n-x86_64.deb"),
              "asset downloaded to updates dir");
        // The post-download auto-install runs /bin/true here (never a
        // real handler) + schedules the production quit, which lands
        // after main() returns and is harmless without a running loop.
    }

    std::printf(failures ? "UPDATER SUITE FAILURES=%d\n"
                         : "UPDATER SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}

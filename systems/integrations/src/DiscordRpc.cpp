#include "nuvio/integrations/DiscordRpc.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDateTime>
#include <optional>
#include <algorithm>
#include <cstdlib>

namespace nuvio::integrations {
namespace discord {

namespace {
constexpr int kMaxPayload = 64 * 1024;
void putInt32(QByteArray& out, qint32 v)
{
    out.append(reinterpret_cast<const char*>(&v), 4);   // x86 LE target ISA
}
}

QByteArray buildFrame(quint32 op, const QByteArray& jsonIn)
{
    QByteArray json = jsonIn;
    if (json.size() > kMaxPayload) json.truncate(kMaxPayload);
    QByteArray out;
    out.reserve(8 + json.size());
    putInt32(out, static_cast<qint32>(op));
    putInt32(out, json.size());
    out.append(json);
    return out;
}

std::optional<std::pair<quint32, QByteArray>>
parseFrame(const QByteArray& buf, int* consumedOut)
{
    if (buf.size() < 8) return std::nullopt;
    qint32 op, len;
    std::memcpy(&op,  buf.constData(),     4);
    std::memcpy(&len, buf.constData() + 4, 4);
    if (len < 0 || len > kMaxPayload) return std::nullopt;   // corrupt guard
    if (buf.size() - 8 < len) return std::nullopt;           // incomplete
    if (consumedOut) *consumedOut = 8 + len;
    return std::make_pair(static_cast<quint32>(op),
                          buf.mid(8, len));
}

QByteArray buildSetActivity(const QString& nonce, const QString& state,
                            const QString& details, qint64 startEpochSec,
                            qint64 endEpochSec)
{
    QJsonObject activity;
    if (!state.isEmpty())   activity.insert(QStringLiteral("state"), state);
    if (!details.isEmpty()) activity.insert(QStringLiteral("details"), details);
    if (startEpochSec >= 0 || endEpochSec >= 0) {
        QJsonObject ts;
        if (startEpochSec >= 0) ts.insert(QStringLiteral("start"), startEpochSec);
        if (endEpochSec   >= 0) ts.insert(QStringLiteral("end"),   endEpochSec);
        activity.insert(QStringLiteral("timestamps"), ts);
    }
    QJsonObject args{{QLatin1String("activity"), activity}};
    QJsonObject body{{QLatin1String("cmd"),    QStringLiteral("SET_ACTIVITY")},
                     {QLatin1String("args"),   args},
                     {QLatin1String("nonce"),  nonce}};
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

} // namespace discord

// ---- runtime manager ---------------------------------------------------------

namespace {
QStringList socketCandidates()
{
    QStringList files{QStringLiteral("discord-ipc-0")};
    for (int i = 1; i < 10; ++i)
        files.append(QStringLiteral("discord-ipc-%1").arg(i));
    const QString xdg = qEnvironmentVariable("XDG_RUNTIME_DIR");
    QStringList out;
    if (!xdg.isEmpty())
        for (const QString& f : files) out.append(xdg + QLatin1Char('/') + f);
    // snap/flatpak-resilient extras (Compose parity, best-effort)
    for (const QString& f : files) {
        out.append(QDir::homePath() + QStringLiteral("/snap/discord/current/.config/discord/") + f);
        out.append(QDir::homePath() + QStringLiteral("/.discord-ipc/") + f);
    }
    return out;
}
} // namespace

DiscordPresence::DiscordPresence(QObject* parent) : QObject(parent)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this,
            &DiscordPresence::connectNow);
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(800);
    connect(&m_debounceTimer, &QTimer::timeout, this,
            &DiscordPresence::flushPending);
}

DiscordPresence::~DiscordPresence()
{
    if (m_socket) m_socket->disconnectFromServer();
}

void DiscordPresence::setClientId(const QString& id)
{
    if (!id.isEmpty()) m_clientId = id;
}

bool DiscordPresence::isConnected() const
{
    return m_socket && m_socket->state() == QLocalSocket::ConnectedState;
}

void DiscordPresence::scheduleConnect()
{
    if (!m_reconnectTimer.isActive()) {
        m_reconnectTimer.start(m_backoffMs);
        m_backoffMs = std::min(m_backoffMs * 2, 60000);   // 1s..60s doubling
    }
}

void DiscordPresence::setTitle(const QString& title)
{
    if (m_title == title) return;
    m_title = title;
    m_debounceTimer.start();     // latest wins; never restarts a send
}

void DiscordPresence::updateProgress(double elapsedSec, double totalSec,
                                     bool paused)
{
    const bool changed = m_elapsed != elapsedSec || m_total != totalSec
                      || m_paused != paused;
    if (!changed) return;
    m_elapsed = elapsedSec;
    m_total   = totalSec;
    m_paused  = paused;
    m_debounceTimer.start();
}

void DiscordPresence::stop()
{
    m_title.clear();
    m_lastKey.clear();
    flushPending();              // sends empty activity = clear presence
    if (m_socket) m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
}

void DiscordPresence::connectNow()
{
    if (isConnected()) return;
    for (const QString& path : socketCandidates()) {
        auto* sock = new QLocalSocket(this);
        sock->connectToServer(path);
        if (!sock->waitForConnected(400)) {      // bounded: no endless queue
            sock->deleteLater();
            continue;
        }
        m_socket  = sock;
        m_backoffMs = 1000;                      // healthy handshake resets

        connect(m_socket, &QLocalSocket::disconnected, this,
                [this] { markDead(); });
        connect(m_socket, &QLocalSocket::errorOccurred, this,
                [this](QLocalSocket::LocalSocketError) { markDead(); });

        // HANDSHAKE first; presence resyncs after DISPATCH arrives.
        const QByteArray hs = discord::buildFrame(
            discord::OpHandshake,
            QJsonDocument(QJsonObject{
                {QLatin1String("v"),         1},
                {QLatin1String("client_id"), m_clientId}})
                .toJson(QJsonDocument::Compact));
        m_socket->write(hs);

        connect(m_socket, &QLocalSocket::readyRead, this, [this] {
            const QByteArray buf = m_socket->readAll();
            int consumed = 0;
            for (QByteArray view = buf;;) {
                const auto parsed = discord::parseFrame(view, &consumed);
                if (!parsed) break;
                const auto [op, json] = *parsed;
                if (op == discord::OpPing)
                    m_socket->write(discord::buildFrame(discord::OpPong,
                                                        json));
                view.remove(0, consumed);
                if (view.isEmpty()) break;
            }
        });
        emit connectionChanged();
        flushPending();                          // resync presence on revive
        return;
    }
    scheduleConnect();                           // nobody home: backoff
}

void DiscordPresence::markDead()
{
    if (!m_socket) return;
    m_socket->deleteLater();
    m_socket = nullptr;
    emit connectionChanged();
    scheduleConnect();
}

QString DiscordPresence::contentKey() const
{
    // Seek detection rides the timestamps: rebuilds when the wall-clock
    // start jumps >4s (Compose parity threshold).
    QString ts = QStringLiteral("-|-|-|-|-");
    if (!m_paused && m_total > 0 && m_elapsed >= 0) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const qint64 start = now - static_cast<qint64>(m_elapsed);
        const qint64 end   = start + static_cast<qint64>(m_total);
        const bool drifted = m_lastStartEpoch >= 0
            && std::llabs(start - m_lastStartEpoch) > 4;
        ts = QStringLiteral("%1|%2|%3").arg(start).arg(end).arg(drifted);
    }
    return QStringLiteral("%1|%2|%3")
        .arg(m_title, m_paused ? QStringLiteral("paused") : "playing", ts);
}

void DiscordPresence::flushPending()
{
    if (!isConnected()) return;
    const QString key = contentKey();
    if (key == m_lastKey) return;               // dedupe: nothing changed

    QString state;
    qint64 start = -1, end = -1;
    if (!m_title.isEmpty()) {
        state = m_title;
        if (!m_paused && m_total > 0 && m_elapsed >= 0) {
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            start = now - static_cast<qint64>(m_elapsed);
            end   = start + static_cast<qint64>(m_total);
            m_lastStartEpoch = start;
        }
    }
    sendFrame(discord::buildFrame(
        discord::OpFrame,
        discord::buildSetActivity(QString::number(
                                      QDateTime::currentMSecsSinceEpoch()),
                                  state, QString(), start, end)));
    m_lastKey = key;
}

void DiscordPresence::sendFrame(const QByteArray& f)
{
    if (isConnected()) m_socket->write(f);
}

} // namespace nuvio::integrations

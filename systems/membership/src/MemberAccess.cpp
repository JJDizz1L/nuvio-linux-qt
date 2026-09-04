#include "nuvio/membership/MemberAccess.h"

#include <QDateTime>
#include <QJsonDocument>

#include "nuvio/authsync/AuthConfig.h"
#include "nuvio/authsync/AuthService.h"
#include "nuvio/authsync/SyncRpcClient.h"
#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::membership {

namespace {

constexpr auto kStoreFile = "member_access";
constexpr auto kPayloadKey = "access_payload";
// Fork VerificationIntervalMs / RetryDelaysMs parity.
constexpr qint64 kVerificationIntervalMs = 15LL * 60LL * 1000LL;
constexpr int kRetryDelaysMs[] = {1000, 2000, 4000};

[[nodiscard]] nuvio::settings::PropertiesStore openStore()
{
    return nuvio::settings::PropertiesStore(
        nuvio::settings::PropertiesStore::defaultPath(kStoreFile));
}

} // namespace

MemberAccess::MemberAccess(nuvio::authsync::AuthService* auth,
                           QObject* parent)
    : QObject(parent), m_auth(auth)
{
    Q_ASSERT(m_auth);
    m_client = new nuvio::authsync::SyncRpcClient(
        nuvio::authsync::AuthConfig::load(),
        [this] { return m_auth->accessToken(); }, this);
    m_verifyTimer.setInterval(kVerificationIntervalMs);
    connect(&m_verifyTimer, &QTimer::timeout, this,
            &MemberAccess::refreshIfStale);
}

QStringList MemberAccess::entitlements() const
{
    QStringList out;
    for (CosmeticEntitlement e : m_entitlements)
        out.append(entitlementName(e));
    return out;
}

bool MemberAccess::hasEntitlement(const QString& name) const
{
    CosmeticEntitlement e;
    if (!entitlementFromName(name.trimmed().toUpper(), e)) return false;
    return m_entitlements.contains(e);
}

void MemberAccess::refresh()
{
    ensureStarted();
    ++m_generation;
    loadAccess(m_generation);
}

void MemberAccess::refreshIfStale()
{
    ensureStarted();
    if (!m_auth->sessionActive()) return;
    const QString userId = m_auth->userId();
    if (userId.isEmpty()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_verifiedUserId != userId ||
        now - m_verifiedAtMs >= kVerificationIntervalMs)
        refresh();
}

void MemberAccess::clearLocalState()
{
    ++m_generation;   // drops in-flight verifications
    setAccess(MemberTier::None, {});
    m_verifiedUserId.clear();
    m_verifiedAtMs = 0;
    openStore().putString(kPayloadKey, std::nullopt);
}

void MemberAccess::ensureStarted()
{
    if (m_started) return;
    m_started = true;
    loadStored();
    if (m_tier != MemberTier::None) emit changed();
    connect(m_auth, &nuvio::authsync::AuthService::stateChanged, this,
            &MemberAccess::onAuthChanged);
    onAuthChanged();
    m_verifyTimer.start();
}

void MemberAccess::onAuthChanged()
{
    if (!m_auth->sessionActive() || m_auth->userId().isEmpty()) {
        ++m_generation;
        setAccess(MemberTier::None, {});
        m_verifiedUserId.clear();
        m_verifiedAtMs = 0;
        return;
    }
    ++m_generation;
    loadAccess(m_generation);
}

void MemberAccess::loadAccess(quint64 generation)
{
    const QString userId = m_auth->userId();
    if (!m_auth->sessionActive() || userId.isEmpty()) {
        if (generation == m_generation)
            setAccess(MemberTier::None, {});
        return;
    }
    // Cached access first (userId-keyed, fork loadCached parity).
    auto store = openStore();
    const auto raw = store.getString(kPayloadKey);
    const StoredAccess cached =
        decodeStoredAccess(raw ? QString::fromStdString(*raw) : QString());
    if (cached.valid && cached.userId == userId &&
        generation == m_generation)
        setAccess(cached.tier, cached.entitlements);
    fetchWithRetry(0, generation);
}

void MemberAccess::fetchWithRetry(int attempt, quint64 generation)
{
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &nuvio::authsync::SyncRpcClient::finished, this,
                   [this, con, attempt, generation](
                       bool ok, int, const QJsonDocument& doc, QByteArray) {
                       disconnect(*con);
                       if (generation != m_generation) return;
                       if (ok) {
                           MemberTier tier = MemberTier::None;
                           QList<CosmeticEntitlement> entitlements;
                           parseMemberAccess(doc.toJson(QJsonDocument::Compact),
                                             tier, entitlements);
                           applyRemote(m_auth->userId(), tier, entitlements,
                                       generation);
                           return;
                       }
                       // Retain cached access on failure (fork parity);
                       // retry on the 1/2/4s schedule, then stop.
                       constexpr int kAttempts =
                           sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0]);
                       if (attempt >= kAttempts) return;
                       QTimer::singleShot(kRetryDelaysMs[attempt], this,
                                          [this, attempt, generation] {
                                              if (generation != m_generation)
                                                  return;
                                              fetchWithRetry(attempt + 1,
                                                             generation);
                                          });
                   });
    m_client->call(QStringLiteral("get_my_member_access"), QJsonObject{});
}

void MemberAccess::applyRemote(const QString& userId, MemberTier tier,
                               const QList<CosmeticEntitlement>& entitlements,
                               quint64 generation)
{
    if (generation != m_generation) return;
    if (userId.isEmpty()) return;
    openStore().putString(
        kPayloadKey,
        encodeStoredAccess(userId, tier, entitlements).toStdString());
    setAccess(tier, entitlements);
    m_verifiedUserId = userId;
    m_verifiedAtMs = QDateTime::currentMSecsSinceEpoch();
}

void MemberAccess::setAccess(MemberTier tier,
                             const QList<CosmeticEntitlement>& entitlements)
{
    if (m_tier == tier && m_entitlements == entitlements) return;
    m_tier = tier;
    m_entitlements = entitlements;
    emit changed();
}

void MemberAccess::loadStored()
{
    auto store = openStore();
    const auto raw = store.getString(kPayloadKey);
    const StoredAccess cached =
        decodeStoredAccess(raw ? QString::fromStdString(*raw) : QString());
    if (!cached.valid) return;
    m_tier = cached.tier;
    m_entitlements = cached.entitlements;
}

} // namespace nuvio::membership

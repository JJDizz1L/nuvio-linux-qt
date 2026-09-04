#include "nuvio/authsync/ProviderCredsController.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "nuvio/mdblist/MdbListSettings.h"
#include "nuvio/settings/AppSettings.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SyncIdentity.h"
#include "nuvio/tmdb/TmdbSettings.h"

namespace nuvio::authsync {

namespace {
constexpr const char* kAnimeSkip = "animeskip";
constexpr const char* kIntroDb = "introdb";
constexpr const char* kTmdb = "tmdb";
constexpr const char* kMdbList = "mdblist";
constexpr const char* kClientIdField = "client_id";
constexpr const char* kApiKeyField = "api_key";
} // namespace

ProviderCredsController::ProviderCredsController(
    settings::AppSettings* settings, tmdb::TmdbSettings* tmdb,
    mdblist::MdbListSettings* mdblist, AuthConfig cfg, TokenProvider token,
    int profileId, QObject* parent)
    : QObject(parent),
      m_settings(settings),
      m_tmdb(tmdb),
      m_mdbList(mdblist),
      m_cfg(std::move(cfg)),
      m_token(std::move(token)),
      m_client(new SyncRpcClient(m_cfg, [this] { return m_token(); }, this)),
      m_profileId(profileId)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(1500);
    connect(&m_debounce, &QTimer::timeout, this,
            &ProviderCredsController::syncNow);
}

ProviderCredsController::~ProviderCredsController() = default;

QString ProviderCredsController::originId()
{
    nuvio::settings::PropertiesStore idStore(
        nuvio::settings::PropertiesStore::defaultPath(
            "sync_client_identity"));
    return nuvio::settings::SyncIdentity::currentClientId(idStore);
}

QJsonObject ProviderCredsController::localSnapshot() const
{
    return QJsonObject{
        {QLatin1String(kAnimeSkip),
         m_settings->animeSkipClientId().trimmed()},
        {QLatin1String(kIntroDb), m_settings->introDbApiKey().trimmed()},
        {QLatin1String(kTmdb), m_tmdb->apiKey().trimmed()},
        {QLatin1String(kMdbList), m_mdbList->apiKey().trimmed()},
    };
}

void ProviderCredsController::onLocalCredsChanged()
{
    if (!m_cfg.valid() || !signedIn()) return;
    if (!m_debounce.isActive()) m_debounce.start();
}

void ProviderCredsController::pushSnapshot(const QJsonObject& snapshot)
{
    QJsonArray creds;
    {
        QJsonObject json;
        json.insert(QLatin1String(kClientIdField),
                    snapshot.value(QLatin1String(kAnimeSkip)).toString());
        creds.append(QJsonObject{
            {QStringLiteral("provider"), QLatin1String(kAnimeSkip)},
            {QStringLiteral("credential_json"), json},
        });
    }
    {
        QJsonObject json;
        json.insert(QLatin1String(kApiKeyField),
                    snapshot.value(QLatin1String(kIntroDb)).toString());
        creds.append(QJsonObject{
            {QStringLiteral("provider"), QLatin1String(kIntroDb)},
            {QStringLiteral("credential_json"), json},
        });
    }
    {
        QJsonObject json;
        json.insert(QLatin1String(kApiKeyField),
                    snapshot.value(QLatin1String(kTmdb)).toString());
        creds.append(QJsonObject{
            {QStringLiteral("provider"), QLatin1String(kTmdb)},
            {QStringLiteral("credential_json"), json},
        });
    }
    {
        QJsonObject json;
        json.insert(QLatin1String(kApiKeyField),
                    snapshot.value(QLatin1String(kMdbList)).toString());
        creds.append(QJsonObject{
            {QStringLiteral("provider"), QLatin1String(kMdbList)},
            {QStringLiteral("credential_json"), json},
        });
    }
    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con, snapshot](bool ok, int,
                                         const QJsonDocument&,
                                         QByteArray) {
                       disconnect(*con);
                       --m_inFlight;
                       if (ok) {
                           m_baseline = snapshot;
                           m_hasBaseline = true;
                       }
                       // No pull-after-push: we own both credential rows,
                       // so the pushed snapshot already is server state.
                       emit syncFinished(ok, false);
                   });
    m_client->call(QString::fromLatin1("sync_push_provider_credentials"),
                   QJsonObject{
                       {QStringLiteral("p_profile_id"), m_profileId},
                       {QStringLiteral("p_credentials"), creds},
                       {QStringLiteral("p_origin_client_id"), originId()},
                   });
}

void ProviderCredsController::syncNow()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) return;
    const QJsonObject local = localSnapshot();
    // Push-if-dirty against the in-memory baseline (first run establishes
    // it without pushing: the pull+merge below adopts server state).
    if (m_hasBaseline && local != m_baseline) {
        pushSnapshot(local);
        return;
    }
    m_hasBaseline = true;
    m_baseline = local;
    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con, local](bool ok, int, const QJsonDocument& doc,
                                      QByteArray) {
                       disconnect(*con);
                       --m_inFlight;
                       if (!ok) {
                           emit syncFinished(false, false);
                           return;
                       }
                       QHash<QString, QJsonObject> rows;
                       const QJsonArray arr =
                           doc.isArray() ? doc.array() : QJsonArray{};
                       for (const QJsonValue& v : arr) {
                           const QJsonObject o = v.toObject();
                           rows.insert(
                               o.value(QStringLiteral("provider"))
                                   .toString()
                                   .toLower(),
                               o.value(QStringLiteral("credential_json"))
                                   .toObject());
                       }
                       const bool serverEmpty = rows.isEmpty();
                       const bool localHasValues =
                           !local.value(QLatin1String(kAnimeSkip))
                                .toString()
                                .isEmpty() ||
                           !local.value(QLatin1String(kIntroDb))
                                .toString()
                                .isEmpty() ||
                           !local.value(QLatin1String(kTmdb))
                                .toString()
                                .isEmpty() ||
                           !local.value(QLatin1String(kMdbList))
                                .toString()
                                .isEmpty();
                       if (serverEmpty && localHasValues) {
                           // Seed: same payload shape, seed RPC semantics.
                           ++m_inFlight;
                           auto seedCon =
                               std::make_shared<QMetaObject::Connection>();
                           *seedCon = connect(
                               m_client, &SyncRpcClient::finished, this,
                               [this, seedCon, local](bool seedOk, int,
                                                      const QJsonDocument&,
                                                      QByteArray) {
                                   disconnect(*seedCon);
                                   --m_inFlight;
                                   if (seedOk) {
                                       m_baseline = local;
                                       m_hasBaseline = true;
                                   }
                                   emit syncFinished(seedOk, false);
                               });
                           QJsonArray creds;
                           for (const QString& provider :
                                {QLatin1String(kAnimeSkip),
                                 QLatin1String(kIntroDb),
                                 QLatin1String(kTmdb),
                                 QLatin1String(kMdbList)}) {
                               QJsonObject json;
                               json.insert(
                                   provider == QLatin1String(kAnimeSkip)
                                       ? QLatin1String(kClientIdField)
                                       : QLatin1String(kApiKeyField),
                                   local.value(provider).toString());
                               creds.append(QJsonObject{
                                   {QStringLiteral("provider"), provider},
                                   {QStringLiteral("credential_json"), json},
                               });
                           }
                           m_client->call(
                               QString::fromLatin1(
                                   "sync_seed_provider_credentials"),
                               QJsonObject{
                                   {QStringLiteral("p_profile_id"),
                                    m_profileId},
                                   {QStringLiteral("p_credentials"), creds},
                                   {QStringLiteral("p_origin_client_id"),
                                    originId()},
                               });
                           return;
                       }
                       // Merge: remote wins per provider when present.
                       bool applied = false;
                       const QString remoteAnime =
                           rows.contains(QLatin1String(kAnimeSkip))
                               ? rows.value(QLatin1String(kAnimeSkip))
                                     .value(QLatin1String(kClientIdField))
                                     .toString()
                                     .trimmed()
                               : local.value(QLatin1String(kAnimeSkip))
                                     .toString();
                       const QString remoteIntro =
                           rows.contains(QLatin1String(kIntroDb))
                               ? rows.value(QLatin1String(kIntroDb))
                                     .value(QLatin1String(kApiKeyField))
                                     .toString()
                                     .trimmed()
                               : local.value(QLatin1String(kIntroDb))
                                     .toString();
                       const QString remoteTmdb =
                           rows.contains(QLatin1String(kTmdb))
                               ? rows.value(QLatin1String(kTmdb))
                                     .value(QLatin1String(kApiKeyField))
                                     .toString()
                                     .trimmed()
                               : local.value(QLatin1String(kTmdb))
                                     .toString();
                       const QString remoteMdbList =
                           rows.contains(QLatin1String(kMdbList))
                               ? rows.value(QLatin1String(kMdbList))
                                     .value(QLatin1String(kApiKeyField))
                                     .toString()
                                     .trimmed()
                               : local.value(QLatin1String(kMdbList))
                                     .toString();
                       if (remoteAnime !=
                           local.value(QLatin1String(kAnimeSkip)).toString()) {
                           m_settings->setAnimeSkipClientId(remoteAnime);
                           applied = true;
                       }
                       if (remoteIntro !=
                           local.value(QLatin1String(kIntroDb)).toString()) {
                           m_settings->setIntroDbApiKey(remoteIntro);
                           applied = true;
                       }
                       if (remoteTmdb !=
                           local.value(QLatin1String(kTmdb)).toString()) {
                           m_tmdb->setApiKey(remoteTmdb);
                           applied = true;
                       }
                       if (remoteMdbList !=
                           local.value(QLatin1String(kMdbList)).toString()) {
                           m_mdbList->setApiKey(remoteMdbList);
                           applied = true;
                       }
                       QJsonObject merged = local;
                       merged.insert(QLatin1String(kAnimeSkip), remoteAnime);
                       merged.insert(QLatin1String(kIntroDb), remoteIntro);
                       merged.insert(QLatin1String(kTmdb), remoteTmdb);
                       merged.insert(QLatin1String(kMdbList), remoteMdbList);
                       m_baseline = merged;
                       m_hasBaseline = true;
                       emit syncFinished(true, applied);
                   });
    m_client->call(QString::fromLatin1("sync_pull_provider_credentials"),
                   QJsonObject{{QStringLiteral("p_profile_id"), m_profileId}});
}

} // namespace nuvio::authsync

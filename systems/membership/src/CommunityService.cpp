#include "nuvio/membership/CommunityService.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrl>

#include "nuvio/membership/Membership.h"

namespace nuvio::membership {

QString contributorSupportLink(const QString& login)
{
    const QString l = login.trimmed().toLower();
    if (l == QLatin1String("skoruppa"))
        return QStringLiteral("https://ko-fi.com/skoruppa");
    if (l == QLatin1String("whitegiso"))
        return QStringLiteral("https://ko-fi.com/whitegiso");
    if (l == QLatin1String("edoedac0"))
        return QStringLiteral("https://ko-fi.com/edoedac");
    if (l == QLatin1String("crisszollo") || l == QLatin1String("xrissozollo"))
        return QStringLiteral("https://ko-fi.com/crisszollo");
    return {};
}

QString formatMembershipLevel(const QString& raw)
{
    QStringList words;
    for (const QString& part : raw.split(u'_')) {
        const QString w = part.trimmed().toLower();
        if (w.isEmpty()) continue;
        words.append(w.at(0).toUpper() + w.mid(1));
    }
    return words.join(u' ');
}

bool normalizeContributor(const QString& name, const QString& avatar,
                          const QString& profile, int total,
                          CommunityContributor& out)
{
    const QString login = name.trimmed();
    if (login.isEmpty() || total <= 0) return false;
    out.login = login;
    const QString av = avatar.trimmed();
    if (!av.isEmpty()) out.avatarUrl = av;
    const QString pr = profile.trimmed();
    if (!pr.isEmpty()) out.profileUrl = pr;
    out.totalContributions = total;
    return true;
}

bool normalizeSupporter(const QString& displayName,
                        const QString& avatarUrl,
                        const QString& membershipLevel,
                        const QString& supporterSince, int index,
                        CommunitySupporter& out)
{
    const QString name = displayName.trimmed();
    if (name.isEmpty()) return false;
    out.name = name;
    const QString av = avatarUrl.trimmed();
    if (!av.isEmpty()) out.avatarUrl = av;
    const QString level = membershipLevel.trimmed();
    out.membershipLevel = level.isEmpty() ? QStringLiteral("SUPPORTER")
                                          : level;
    const QString since = supporterSince.trimmed();
    if (!since.isEmpty()) out.supporterSince = since;
    // Uniqueness suffix (fork mapIndexed parity).
    out.key = QStringLiteral("%1-%2#%3")
                  .arg(name.toLower(), out.supporterSince)
                  .arg(index);
    return true;
}

QVariantList sortContributors(const QList<CommunityContributor>& rows)
{
    QList<CommunityContributor> sorted = rows;
    std::sort(sorted.begin(), sorted.end(),
              [](const CommunityContributor& a,
                 const CommunityContributor& b) {
                  if (a.totalContributions != b.totalContributions)
                      return a.totalContributions > b.totalContributions;
                  return a.login.toLower() < b.login.toLower();
              });
    QVariantList out;
    for (const CommunityContributor& c : sorted)
        out.append(contributorVariant(c));
    return out;
}

QVariantMap contributorVariant(const CommunityContributor& c)
{
    return QVariantMap{
        {QStringLiteral("login"), c.login},
        {QStringLiteral("avatarUrl"), c.avatarUrl},
        {QStringLiteral("profileUrl"), c.profileUrl},
        {QStringLiteral("totalContributions"), c.totalContributions},
    };
}

QVariantMap supporterVariant(const CommunitySupporter& s)
{
    return QVariantMap{
        {QStringLiteral("key"), s.key},
        {QStringLiteral("name"), s.name},
        {QStringLiteral("avatarUrl"), s.avatarUrl},
        {QStringLiteral("membershipLevel"), s.membershipLevel},
        {QStringLiteral("membershipLevelDisplay"),
         formatMembershipLevel(s.membershipLevel)},
        {QStringLiteral("supporterSince"), s.supporterSince},
        {QStringLiteral("supporterSinceDisplay"),
         s.supporterSince.isEmpty()
             ? QString()
             : formatDonationDate(s.supporterSince)},
    };
}

QList<CommunityContributor> parseContributors(const QByteArray& body)
{
    QList<CommunityContributor> out;
    const QJsonArray arr = QJsonDocument::fromJson(body)
                               .object()
                               .value(QStringLiteral("contributors"))
                               .toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        CommunityContributor c;
        if (normalizeContributor(o.value(QStringLiteral("name")).toString(),
                                 o.value(QStringLiteral("avatar")).toString(),
                                 o.value(QStringLiteral("profile")).toString(),
                                 o.value(QStringLiteral("total")).toInt(0),
                                 c))
            out.append(c);
    }
    return out;
}

QList<CommunitySupporter> parseSupportersWall(const QByteArray& body)
{
    QList<CommunitySupporter> out;
    const QJsonArray members = QJsonDocument::fromJson(body)
                                   .object()
                                   .value(QStringLiteral("top"))
                                   .toObject()
                                   .value(QStringLiteral("members"))
                                   .toArray();
    int index = 0;
    for (const QJsonValue& v : members) {
        const QJsonObject o = v.toObject();
        CommunitySupporter s;
        if (normalizeSupporter(
                o.value(QStringLiteral("displayName")).toString(),
                o.value(QStringLiteral("avatarUrl")).toString(),
                o.value(QStringLiteral("membershipLevel")).toString(),
                o.value(QStringLiteral("supporterSince")).toString(), index,
                s))
            out.append(s);
        ++index;
    }
    return out;
}

CommunityService::CommunityService(QObject* parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    if (const QByteArray env = qgetenv("NUVIO_CONTRIBUTIONS_URL");
        !env.isEmpty())
        m_contributionsUrl = QString::fromUtf8(env);
    if (const QByteArray env = qgetenv("NUVIO_SUPPORTERS_WALL_URL");
        !env.isEmpty())
        m_supportersWallUrl = QString::fromUtf8(env);
}

void CommunityService::loadContributors(bool force)
{
    if (m_contributorsLoading) return;
    if (!force && m_contributorsLoaded) return;
    if (m_contributionsUrl.trimmed().isEmpty()) {
        // Unconfigured (default build): honest empty, fork parity.
        m_contributorsLoading = false;
        m_contributorsLoaded = false;
        m_contributors.clear();
        m_contributorsError =
            tr("Unable to load contributors.");
        emit changed();
        return;
    }
    m_contributorsLoading = true;
    m_contributorsError.clear();
    emit changed();
    QNetworkReply* rep =
        m_nam->get(QNetworkRequest{QUrl(m_contributionsUrl.trimmed())});
    connect(rep, &QNetworkReply::finished, this, [this, rep] {
        rep->deleteLater();
        m_contributorsLoading = false;
        if (rep->error() == QNetworkReply::NoError) {
            m_contributors =
                sortContributors(parseContributors(rep->readAll()));
            m_contributorsLoaded = true;
            m_contributorsError.clear();
        } else {
            m_contributorsLoaded = false;
            m_contributors.clear();
            m_contributorsError =
                tr("Couldn't load contributors right now.");
        }
        emit changed();
    });
}

void CommunityService::loadSupporters(bool force)
{
    if (m_supportersLoading) return;
    if (!force && m_supportersLoaded) return;
    if (m_supportersWallUrl.trimmed().isEmpty()) {
        m_supportersLoading = false;
        m_supportersLoaded = false;
        m_supporters.clear();
        m_supportersError = tr("Supporters API is not configured.");
        emit changed();
        return;
    }
    m_supportersLoading = true;
    m_supportersError.clear();
    emit changed();
    QNetworkReply* rep =
        m_nam->get(QNetworkRequest{QUrl(m_supportersWallUrl.trimmed())});
    connect(rep, &QNetworkReply::finished, this, [this, rep] {
        rep->deleteLater();
        m_supportersLoading = false;
        if (rep->error() == QNetworkReply::NoError) {
            QVariantList rows;
            for (const CommunitySupporter& s :
                 parseSupportersWall(rep->readAll()))
                rows.append(supporterVariant(s));
            m_supporters = rows;
            m_supportersLoaded = true;
            m_supportersError.clear();
        } else {
            m_supportersLoaded = false;
            m_supporters.clear();
            m_supportersError =
                tr("Couldn't load supporters right now.");
        }
        emit changed();
    });
}

} // namespace nuvio::membership

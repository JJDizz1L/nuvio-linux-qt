#pragma once

// Community contributors + supporters wall (fork
// SupportersContributorsRepository parity): plain-HTTP DTOs with the
// verbatim normalize/sort rules. Build-time URLs become env-overridable
// constants (fork CommunityConfig parity): contributions blank by
// default (contributors tab reports unconfigured), the supporters wall
// defaults to the nuvio.tv endpoint. Donation progress is skipped:
// desktop policy disables it, so no donations UI ships here.

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace nuvio::membership {

/// ko-fi links for known contributor logins (verbatim table).
[[nodiscard]] QString contributorSupportLink(const QString& login);
/// "SUPPORTER_PLUS" -> "Supporter Plus" (fork formatMembershipLevel).
[[nodiscard]] QString formatMembershipLevel(const QString& raw);

struct CommunityContributor {
    QString login;
    QString avatarUrl;
    QString profileUrl;
    int totalContributions = 0;
};

struct CommunitySupporter {
    QString key;
    QString name;
    QString avatarUrl;
    QString membershipLevel;
    QString supporterSince;   // raw ISO (formatted at display time)
};

/// Null when the row is unusable (blank login / no contributions).
[[nodiscard]] bool normalizeContributor(const QString& name,
                                        const QString& avatar,
                                        const QString& profile, int total,
                                        CommunityContributor& out);
[[nodiscard]] bool normalizeSupporter(const QString& displayName,
                                      const QString& avatarUrl,
                                      const QString& membershipLevel,
                                      const QString& supporterSince,
                                      int index,
                                      CommunitySupporter& out);
/// Contribution-descending, login-ascending (fork sort parity).
[[nodiscard]] QVariantList sortContributors(
    const QList<CommunityContributor>& rows);
[[nodiscard]] QVariantMap contributorVariant(
    const CommunityContributor& c);
[[nodiscard]] QVariantMap supporterVariant(const CommunitySupporter& s);
[[nodiscard]] QList<CommunityContributor> parseContributors(
    const QByteArray& body);
[[nodiscard]] QList<CommunitySupporter> parseSupportersWall(
    const QByteArray& body);

class CommunityService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList contributors READ contributors NOTIFY changed)
    Q_PROPERTY(QVariantList supporters READ supporters NOTIFY changed)
    Q_PROPERTY(bool contributorsLoading READ contributorsLoading NOTIFY changed)
    Q_PROPERTY(bool supportersLoading READ supportersLoading NOTIFY changed)
    Q_PROPERTY(QString contributorsError READ contributorsError NOTIFY changed)
    Q_PROPERTY(QString supportersError READ supportersError NOTIFY changed)

public:
    explicit CommunityService(QObject* parent = nullptr);

    [[nodiscard]] QVariantList contributors() const { return m_contributors; }
    [[nodiscard]] QVariantList supporters() const { return m_supporters; }
    [[nodiscard]] bool contributorsLoading() const
    {
        return m_contributorsLoading;
    }
    [[nodiscard]] bool supportersLoading() const
    {
        return m_supportersLoading;
    }
    [[nodiscard]] QString contributorsError() const
    {
        return m_contributorsError;
    }
    [[nodiscard]] QString supportersError() const
    {
        return m_supportersError;
    }

    Q_INVOKABLE void loadContributors(bool force = false);
    Q_INVOKABLE void loadSupporters(bool force = false);

    /// Test seams (default: contributions blank, wall nuvio.tv).
    void setContributionsUrl(const QString& url) { m_contributionsUrl = url; }
    void setSupportersWallUrl(const QString& url)
    {
        m_supportersWallUrl = url;
    }

signals:
    void changed();

private:
    QNetworkAccessManager* m_nam = nullptr;
    QString m_contributionsUrl;
    QString m_supportersWallUrl =
        QStringLiteral("https://nuvio.tv/api/supporters/wall");

    QVariantList m_contributors;
    QVariantList m_supporters;
    bool m_contributorsLoading = false;
    bool m_supportersLoading = false;
    bool m_contributorsLoaded = false;
    bool m_supportersLoaded = false;
    QString m_contributorsError;
    QString m_supportersError;
};

} // namespace nuvio::membership

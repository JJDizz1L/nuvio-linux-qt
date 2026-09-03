#include "nuvio/playback/ParentalGuide.h"

#include <algorithm>

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>

#include "nuvio/playback/NextEpisodeRules.h"

namespace nuvio::playback {

namespace {
constexpr auto kBase = "https://api.tiffara.com/titles/";

[[nodiscard]] std::optional<QString> dominantSeverity(const QJsonObject& cat)
{
    const QJsonArray rows =
        cat.value(QStringLiteral("severityBreakdowns")).toArray();
    QString best;
    int bestVotes = -1;
    int noneVotes = 0;
    for (const QJsonValue& r : rows) {
        const QJsonObject b = r.toObject();
        const QString level =
            b.value(QStringLiteral("severityLevel")).toString().toLower();
        const int votes = b.value(QStringLiteral("voteCount")).toInt();
        if (level == QLatin1String("none")) {
            noneVotes = votes;
            continue;
        }
        if (votes > bestVotes) {
            bestVotes = votes;
            best = level;
        }
    }
    if (best.isEmpty() || bestVotes <= noneVotes) return std::nullopt;
    return best;
}

[[nodiscard]] QString labelFor(const QString& category)
{
    if (category == QLatin1String("nudity")) return QStringLiteral("Nudity");
    if (category == QLatin1String("violence"))
        return QStringLiteral("Violence");
    if (category == QLatin1String("profanity"))
        return QStringLiteral("Profanity");
    if (category == QLatin1String("alcohol")) return QStringLiteral("Alcohol");
    if (category == QLatin1String("frightening"))
        return QStringLiteral("Frightening");
    return category;
}

[[nodiscard]] QString severityLabel(const QString& severity)
{
    if (severity == QLatin1String("severe"))
        return QStringLiteral("Severe");
    if (severity == QLatin1String("moderate"))
        return QStringLiteral("Moderate");
    if (severity == QLatin1String("mild")) return QStringLiteral("Mild");
    return severity;
}

[[nodiscard]] int severityRank(const QString& severity)
{
    if (severity == QLatin1String("severe")) return 0;
    if (severity == QLatin1String("moderate")) return 1;
    if (severity == QLatin1String("mild")) return 2;
    return 3;
}
} // namespace

QList<ParentalWarning> parseParentalGuide(const QByteArray& body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QJsonArray cats =
        root.value(QStringLiteral("parentsGuide")).toArray();
    if (cats.isEmpty()) return {};

    struct Row {
        QString category;
        QString severity;
    };
    QList<Row> rows;
    for (const QJsonValue& c : cats) {
        const QJsonObject o = c.toObject();
        const QString wireCat = o.value(QStringLiteral("category")).toString();
        if (wireCat.isEmpty()) continue;
        const auto sev = dominantSeverity(o);
        if (!sev) continue;
        const QString upper = wireCat.toUpper();
        QString key;
        if (upper == QLatin1String("SEXUAL_CONTENT")) key = "nudity";
        else if (upper == QLatin1String("VIOLENCE")) key = "violence";
        else if (upper == QLatin1String("PROFANITY")) key = "profanity";
        else if (upper == QLatin1String("ALCOHOL_DRUGS")) key = "alcohol";
        else if (upper == QLatin1String("FRIGHTENING_INTENSE_SCENES"))
            key = "frightening";
        else
            continue;
        rows.append({key, *sev});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return severityRank(a.severity) < severityRank(b.severity);
    });
    QList<ParentalWarning> out;
    for (const Row& r : rows.mid(0, 5))
        out.append({labelFor(r.category), severityLabel(r.severity)});
    return out;
}

ParentalGuideResolver::ParentalGuideResolver(QObject* parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{}

void ParentalGuideResolver::fetch(const QString& id)
{
    const auto tt = extractImdbId(id);
    if (!tt) {
        emit failed();
        return;
    }
    if (m_cache.contains(*tt)) {
        emit resolved(m_cache.value(*tt));
        return;
    }
    if (m_inFlight) return;   // one lookup at a time; caller retries
    m_inFlight = true;
    const QUrl url(QString::fromLatin1(kBase) + *tt +
                   QStringLiteral("/parentsGuide"));
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    QNetworkReply* rep = m_nam->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, rep, tt] {
        rep->deleteLater();
        m_inFlight = false;
        const bool httpOk =
            rep->error() == QNetworkReply::NoError &&
            rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >=
                200 &&
            rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() <
                300;
        if (!httpOk) {
            emit failed();
            return;
        }
        QVariantList warnings;
        for (const ParentalWarning& w : parseParentalGuide(rep->readAll()))
            warnings.append(QVariantMap{{QStringLiteral("label"), w.label},
                                        {QStringLiteral("severity"),
                                         w.severity}});
        m_cache.insert(*tt, warnings);   // cache empties too (negative)
        emit resolved(warnings);
    });
}

} // namespace nuvio::playback

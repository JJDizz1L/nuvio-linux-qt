#include "nuvio/trailer/TrailerKernel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

namespace nuvio::trailer {

const QVector<YouTubeClient>& clients()
{
    static const QVector<YouTubeClient> table = {
        // VERBATIM from CLIENTS (keep both sides in sync).
        {"visionos", "101", "1.02",
         "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15"
         " (KHTML, like Gecko) Version/26.0 Safari/605.1.15",
         QVariantMap{{"clientName", "VISIONOS"}, {"clientVersion", "1.02"},
                     {"deviceMake", "Apple"},
                     {"deviceModel", "RealityDevice17,1"},
                     {"osName", "visionOS"}, {"osVersion", "26.5.23O471"},
                     {"hl", "en"}, {"gl", "US"}}},
        {"android_vr", "28", "1.56.21",
         "com.google.android.apps.youtube.vr.oculus/1.56.21 "
         "(Linux; U; Android 12; en_US; Quest 3; Build/SQ3A.220605.009.A1)"
         " gzip",
         QVariantMap{{"clientName", "ANDROID_VR"},
                     {"clientVersion", "1.56.21"}, {"deviceMake", "Oculus"},
                     {"deviceModel", "Quest 3"}, {"osName", "Android"},
                     {"osVersion", "12"}, {"platform", "MOBILE"},
                     {"androidSdkVersion", 32}, {"hl", "en"}, {"gl", "US"}}},
        {"android", "3", "20.10.35",
         "com.google.android.youtube/20.10.35 (Linux; U; Android 14; en_US)"
         " gzip",
         QVariantMap{{"clientName", "ANDROID"},
                     {"clientVersion", "20.10.35"}, {"osName", "Android"},
                     {"osVersion", "14"}, {"platform", "MOBILE"},
                     {"androidSdkVersion", 34}, {"hl", "en"}, {"gl", "US"}}},
        {"ios", "5", "20.10.1",
         "com.google.ios.youtube/20.10.1 "
         "(iPhone16,2; U; CPU iOS 17_4 like Mac OS X)",
         QVariantMap{{"clientName", "IOS"},
                     {"clientVersion", "20.10.1"},
                     {"deviceModel", "iPhone16,2"}, {"osName", "iPhone"},
                     {"osVersion", "17.4.0.21E219"}, {"platform", "MOBILE"},
                     {"hl", "en"}, {"gl", "US"}}},
    };
    return table;
}

QString fallbackInnertubeApiKey()
{
    // FALLBACK_INNERTUBE_API_KEY verbatim.
    return QStringLiteral("AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8");
}

QString extractVideoId(const QString& urlOrId)
{
    static const QRegularExpression bare(
        QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    const QString trimmed = urlOrId.trimmed();
    if (bare.match(trimmed).hasMatch()) return trimmed;

    static const QRegularExpression hostAndId(
        QStringLiteral("(?:youtu\\.be/|v=|/embed/|/shorts/)"
                       "([A-Za-z0-9_-]{11})"));
    return hostAndId.match(trimmed).hasMatch()
               ? hostAndId.match(trimmed).captured(1)
               : QString{};
}

QByteArray playerRequestBody(const YouTubeClient& client,
                             const QString& videoId,
                             const QString& visitorData)
{
    const QJsonObject clientCtx = QJsonObject::fromVariantMap(client.context);
    QJsonObject ctx;
    ctx.insert(QLatin1String("client"), clientCtx);
    if (!visitorData.isEmpty())
        ctx.insert(QLatin1String("visitorData"), visitorData);
    const QJsonObject body{
        {QLatin1String("context"), ctx},
        {QLatin1String("videoId"), videoId},
        {QLatin1String("contentCheckOk"), true},
        {QLatin1String("racyCheckOk"), true},
    };
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QVector<QPair<QString, QString>> playerRequestHeaders(
    const YouTubeClient& client, const QString& visitorData)
{
    QVector<QPair<QString, QString>> h;
    h.append({QStringLiteral("content-type"),
              QStringLiteral("application/json")});
    h.append({QStringLiteral("origin"),
              QStringLiteral("https://www.youtube.com")});
    h.append({QStringLiteral("x-youtube-client-name"),
              QString::fromLatin1(client.id)});
    h.append({QStringLiteral("x-youtube-client-version"),
              QString::fromLatin1(client.version)});
    h.append({QStringLiteral("user-agent"), client.userAgent});
    if (!visitorData.isEmpty())
        h.append({QStringLiteral("x-goog-visitor-id"), visitorData});
    return h;
}

// ---- streamingData parsing --------------------------------------------------

namespace {
double numOr(const QJsonObject& o, const char* key, double def)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : def;
}

int qualityLabelHeight(const QString& label)
{
    static const QRegularExpression re(QStringLiteral("(\\d+)"));
    const auto m = re.match(label);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

int containerPref(const QString& ext)
{
    if (ext == QLatin1String("mp4") || ext == QLatin1String("m4a")) return 0;
    if (ext == QLatin1String("webm")) return 1;
    return 2;
}
} // namespace

StreamingBuckets parseStreamingData(const QByteArray& body)
{
    StreamingBuckets out;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonObject sd =
        doc.object().value(QLatin1String("streamingData")).toObject();
    if (sd.isEmpty()) return out;

    out.hlsManifestUrl =
        sd.value(QLatin1String("hlsManifestUrl")).toString();

    for (const auto& v : sd.value(QLatin1String("formats")).toArray()) {
        const QJsonObject f = v.toObject();
        const QString url   = f.value(QLatin1String("url")).toString();
        if (url.isEmpty()) continue;
        const QString mime =
            f.value(QLatin1String("mimeType")).toString();
        if (!mime.contains(QLatin1String("video/"))
            && !mime.isEmpty())
            continue;

        StreamCandidate c;
        c.url      = url;
        c.mimeType = mime;
        const double bitrateD =
            numOr(f, "bitrate", 0) != 0 ? numOr(f, "bitrate", 0)
                                        : numOr(f, "averageBitrate", 0);
        c.bitrate = static_cast<qint64>(bitrateD);
        c.height  = qualityLabelHeight(
            f.value(QLatin1String("qualityLabel")).toString());
        c.ext   = mime.contains(QLatin1String("webm"))
                      ? QStringLiteral("webm")
                      : QStringLiteral("mp4");
        c.score = c.height * 1e9 + bitrateD;
        out.progressive.append(c);
    }

    for (const auto& v : sd.value(QLatin1String("adaptiveFormats")).toArray()) {
        const QJsonObject f = v.toObject();
        const QString url = f.value(QLatin1String("url")).toString();
        if (url.isEmpty()) continue;
        const QString mime =
            f.value(QLatin1String("mimeType")).toString();
        const bool hasVideo =
            mime.contains(QLatin1String("video/"));
        const bool hasAudio = mime.startsWith(QLatin1String("audio/"));
        if (!hasVideo && !hasAudio) continue;

        const double bitrateD =
            numOr(f, "bitrate", 0) != 0 ? numOr(f, "bitrate", 0)
                                        : numOr(f, "averageBitrate", 0);

        StreamCandidate c;
        c.client   = QStringLiteral("(unset)");
        c.url      = url;
        c.mimeType = mime;
        c.hasN     = url.contains(QLatin1String("n="));   // approx; slice-2 exact
        c.bitrate  = static_cast<qint64>(bitrateD);

        if (hasVideo) {
            const double fps   = numOr(f, "fps", 0);
            double height      = numOr(f, "height", -1);
            if (height < 0)
                height = qualityLabelHeight(
                    f.value(QLatin1String("qualityLabel")).toString());
            c.height = static_cast<int>(height);
            c.fps    = static_cast<int>(fps);
            c.score  = height * 1e9 + fps * 1e6 + bitrateD;
            c.ext    = mime.contains(QLatin1String("webm"))
                           ? QStringLiteral("webm")
                           : QStringLiteral("mp4");
            out.video.append(c);
        } else {
            const double sampleRate =
                numOr(f, "audioSampleRate", 0);
            c.score = bitrateD * 1e6 + sampleRate;
            c.ext   = mime.contains(QLatin1String("webm"))
                          ? QStringLiteral("webm")
                          : QStringLiteral("m4a");
            out.audio.append(c);
        }
    }
    return out;
}

int containerPreference(const QString& ext) { return containerPref(ext); }

QList<StreamCandidate> sortCandidates(const QList<StreamCandidate>& items)
{
    QList<StreamCandidate> out = items;
    std::sort(out.begin(), out.end(),
              [](const StreamCandidate& a, const StreamCandidate& b) {
                  if (a.score != b.score) return a.score > b.score;
                  if (a.hasN != b.hasN) return !a.hasN;     // no-n preferred
                  const int ca = containerPref(a.ext);
                  const int cb = containerPref(b.ext);
                  if (ca != cb) return ca < cb;
                  return a.priority < b.priority;
              });
    return out;
}

QList<StreamCandidate> orderSeparate(const QList<StreamCandidate>& items)
{
    QList<StreamCandidate> preferred, rest;
    for (const auto& i : items)
        (i.client == QLatin1String("visionos") ? preferred : rest).append(i);
    return sortCandidates(preferred) + sortCandidates(rest);
}

std::optional<PlaybackSource> buildPlaybackSource(
    const QList<StreamCandidate>& orderedProgressive,
    const QList<StreamCandidate>& orderedVideo,
    const QList<StreamCandidate>& orderedAudio,
    const QString& hlsManifestUrl)
{
    // Tier order IS the Compose policy (REVIEW-NOTES T1). Reachability
    // probing lives in the resolver (slice 3); the top of each ordered
    // chain is taken verbatim and probed there.
    if (!orderedVideo.isEmpty() && !orderedAudio.isEmpty())
        return PlaybackSource{QStringLiteral("adaptive_separate"),
                              orderedVideo.first().url,
                              orderedAudio.first().url};
    if (!orderedProgressive.isEmpty())
        return PlaybackSource{QStringLiteral("progressive"),
                              orderedProgressive.first().url, {}};
    if (!hlsManifestUrl.isEmpty())
        return PlaybackSource{QStringLiteral("hls_last_resort"),
                              hlsManifestUrl, {}};
    if (!orderedVideo.isEmpty())
        return PlaybackSource{QStringLiteral("adaptive_video_only"),
                              orderedVideo.first().url, {}};
    return std::nullopt;
}

QStringList hostRotationCandidates(const QString& url)
{
    if (!url.contains(QLatin1String("googlevideo.com")))
        return {url};

    const QUrl parsedUrl(url);
    if (!parsedUrl.isValid() || parsedUrl.host().isEmpty())
        return {url};

    // `mn` = comma-separated alternate server tokens (Compose parity). Each
    // alternates the host's `sn-` token with that server, renumbering the
    // optional `rrN---` prefix to its 1-based index.
    const QStringList servers =
        QUrlQuery(parsedUrl).queryItemValue(QLatin1String("mn"))
            .split(QLatin1Char(','), Qt::SkipEmptyParts);

    static const QRegularExpression rr(QStringLiteral("^rr\\d+---"));
    static const QRegularExpression sn(
        QStringLiteral("sn-[a-z0-9]+-[a-z0-9]+"));

    const QString host = parsedUrl.host();
    QStringList out;
    out.append(url);
    for (int i = 0; i < servers.size(); ++i) {
        // Strip a trailing ".googlevideo.com" from the server token so it
        // swaps cleanly into a host that already carries the domain (Compose
        // naively inserts the FULL server string here, doubling the domain
        // for rrN--- hosts - masked there only because the original URL wins
        // the parallel probe first; we build VALID alternates instead).
        QString token = servers.at(i);
        if (token.startsWith(QLatin1String("sn-")))
            token = token.section(QLatin1Char('.'), 0, 0);

        QString altHost = host;
        altHost.replace(rr,
                        QStringLiteral("rr%1---").arg(i + 1));
        altHost.replace(sn, token);
        if (altHost == host) continue;
        QUrl alt = parsedUrl;
        alt.setHost(altHost);
        out.append(alt.toString());
    }

    // Preserve order, drop duplicates (Compose `.distinct()`).
    QStringList distinct;
    for (const QString& c : std::as_const(out))
        if (!distinct.contains(c)) distinct.append(c);
    return distinct;
}

} // namespace nuvio::trailer

#include "nuvio/notifications/ReleaseDate.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>

namespace nuvio::notifications {

namespace {

const QRegularExpression kIsoDate(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
const QRegularExpression kEmbeddedDate(
    QStringLiteral("(?<!\\d)\\d{4}-\\d{2}-\\d{2}(?!\\d)"));
const QRegularExpression kZonedDateTime(QStringLiteral(
    "^(\\d{4})-(\\d{2})-(\\d{2})T(\\d{2}):(\\d{2}):(\\d{2})"
    "(?:\\.(\\d{1,9}))?(Z|[+-]\\d{2}:?\\d{2})$"));
const QRegularExpression kLocalDateTime(QStringLiteral(
    "^(\\d{4})-(\\d{2})-(\\d{2})T(\\d{2}):(\\d{2}):(\\d{2})"
    "(?:\\.(\\d{1,9}))?$"));

[[nodiscard]] bool validParts(int y, int mo, int d)
{
    return QDate::isValid(y, mo, d);
}

} // namespace

QString parseIsoCalendarDate(const QString& value)
{
    const QString v = value.trimmed();
    if (!kIsoDate.match(v).hasMatch()) return {};
    const int y = v.mid(0, 4).toInt();
    const int mo = v.mid(5, 2).toInt();
    const int d = v.mid(8, 2).toInt();
    if (!validParts(y, mo, d)) return {};
    return v;
}

QString parseEpisodeReleaseLocalDate(const QString& raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty()) return {};

    // Plain calendar dates are preserved as supplied (no timezone).
    if (const QString plain = parseIsoCalendarDate(value);
        !plain.isEmpty())
        return plain;

    // Zoned timestamps use their exact instant, rendered local.
    if (kZonedDateTime.match(value).hasMatch()) {
        // Qt's ISO parser wants the colon form; the fork also accepts
        // bare ±hhmm offsets, so normalize before parsing.
        static const QRegularExpression kBareOffset(
            QStringLiteral("([+-]\\d{2})(\\d{2})$"));
        QString normalized = value;
        normalized.replace(kBareOffset, QStringLiteral("\\1:\\2"));
        const QDateTime zoned =
            QDateTime::fromString(normalized, Qt::ISODateWithMs);
        if (zoned.isValid())
            return zoned.toLocalTime().date().toString(Qt::ISODate);
        const QDateTime zoned2 =
            QDateTime::fromString(normalized, Qt::ISODate);
        if (zoned2.isValid())
            return zoned2.toLocalTime().date().toString(Qt::ISODate);
    }

    // Zone-less timestamps are interpreted in the viewer's timezone;
    // only the date part survives (fork: take(10) re-validated).
    if (kLocalDateTime.match(value).hasMatch())
        return parseIsoCalendarDate(value.left(10));

    const QRegularExpressionMatch embedded = kEmbeddedDate.match(value);
    if (embedded.hasMatch())
        return parseIsoCalendarDate(embedded.captured(0));
    return {};
}

QString todayIsoDate()
{
    return QDate::currentDate().toString(Qt::ISODate);
}

QString isoDateFromEpochMs(qint64 epochMs)
{
    return QDateTime::fromMSecsSinceEpoch(epochMs)
        .toLocalTime()
        .date()
        .toString(Qt::ISODate);
}

} // namespace nuvio::notifications

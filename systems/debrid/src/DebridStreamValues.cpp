#include "nuvio/debrid/DebridStreamValues.h"

#include <QRegularExpression>

#include "nuvio/debrid/DebridTypes.h"
#include "nuvio/debrid/StreamTemplateEngine.h"

namespace nuvio::debrid {

QVariantMap debridStreamValues(const QString& title,
                               const QString& sourceName,
                               const QString& providerId)
{
    QString shortName;
    QString displayName;
    for (const DebridProvider& p : allProviders()) {
        if (p.id == providerId) {
            shortName = p.shortName;
            displayName = p.displayName;
        }
    }
    return QVariantMap{
        {QStringLiteral("stream.title"), title},
        {QStringLiteral("stream.type"), QStringLiteral("Debrid")},
        {QStringLiteral("service.shortName"), shortName},
        {QStringLiteral("service.name"), displayName},
        {QStringLiteral("addon.name"), sourceName},
    };
}

QString formatStreamName(const StreamTemplateEngine& engine,
                         const QString& nameTemplate,
                         const QVariantMap& values, const QString& fallback)
{
    const QString rendered = engine.render(nameTemplate, values);
    QString out;
    const QStringList lines = rendered.split(u'\n');
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0) out += u' ';
        out += lines[i].trimmed();
    }
    out = out.simplified();
    return out.isEmpty() ? fallback : out;
}

QString formatStreamDescription(const StreamTemplateEngine& engine,
                                const QString& descriptionTemplate,
                                const QVariantMap& values,
                                const QString& fallback)
{
    QStringList kept;
    for (const QString& line : descriptionTemplate.isEmpty()
                                       ? QStringList{}
                                       : engine.render(descriptionTemplate,
                                                       values)
                                             .split(u'\n')) {
        const QString t = line.trimmed();
        if (!t.isEmpty()) kept.append(t);
    }
    const QString out = kept.join(u'\n').trimmed();
    return out.isEmpty() ? fallback : out;
}

} // namespace nuvio::debrid

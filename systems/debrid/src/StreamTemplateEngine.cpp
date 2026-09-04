#include "nuvio/debrid/StreamTemplateEngine.h"

#include <algorithm>
#include <cmath>

#include <QRegularExpression>

namespace nuvio::debrid {

QString StreamTemplateEngine::render(const QString& tmplate,
                                     const QVariantMap& values) const
{
    if (tmplate.isEmpty()) return {};
    QString out;
    int index = 0;
    while (index < tmplate.size()) {
        const int start = tmplate.indexOf(u'{', index);
        if (start < 0) {
            out.append(tmplate.mid(index));
            break;
        }
        out.append(tmplate.mid(index, start - index));
        const int end = findPlaceholderEnd(tmplate, start + 1);
        if (end < 0) {
            out.append(tmplate.mid(start));
            break;
        }
        out.append(renderExpression(tmplate.mid(start + 1, end - start - 1),
                                    values));
        index = end + 1;
    }
    return out;
}

QString StreamTemplateEngine::renderExpression(
    const QString& expression, const QVariantMap& values) const
{
    const int bracket = findTopLevelChar(expression, u'[');
    if (bracket >= 0 && expression.endsWith(u']')) {
        const QString condition = expression.left(bracket);
        const auto branches = parseBranches(
            expression.mid(bracket + 1, expression.size() - bracket - 2));
        return render(evaluateCondition(condition, values) ? branches.first
                                                           : branches.second,
                      values);
    }
    const QStringList tokens = splitOps(expression);
    if (tokens.isEmpty()) return {};
    QVariant value = values.value(tokens.first());
    for (int i = 1; i < tokens.size(); ++i)
        value = applyTransform(value, tokens[i]);
    return valueToText(value);
}

bool StreamTemplateEngine::evaluateCondition(const QString& expression,
                                             const QVariantMap& values) const
{
    QStringList flat;
    for (const QString& t : splitOps(expression)) {
        if (!t.trimmed().isEmpty()) flat.append(t);
    }
    if (flat.isEmpty()) return false;
    QList<QList<bool>> groups;
    QList<bool> current;
    int index = 0;
    while (index < flat.size()) {
        if (flat[index] == QLatin1String("or")) {
            groups.append(current);
            current.clear();
            ++index;
        } else if (flat[index] == QLatin1String("and")) {
            ++index;
        } else {
            const QString field = flat[index++];
            QStringList ops;
            while (index < flat.size() && flat[index] != QLatin1String("and") &&
                   flat[index] != QLatin1String("or") &&
                   !isFieldPath(flat[index]))
                ops.append(flat[index++]);
            current.append(evaluateSingle(values.value(field), ops));
        }
    }
    groups.append(current);
    for (const QList<bool>& group : groups) {
        if (group.isEmpty()) continue;
        bool all = true;
        for (bool b : group) all = all && b;
        if (all) return true;
    }
    return false;
}

bool StreamTemplateEngine::evaluateSingle(const QVariant& value,
                                          const QStringList& ops) const
{
    if (ops.isEmpty()) return isTruthy(value);
    bool result = false;
    bool hasResult = false;
    for (const QString& op : ops) {
        if (op == QLatin1String("exists")) {
            result = existsValue(value);
            hasResult = true;
        } else if (op == QLatin1String("istrue")) {
            result = hasResult ? result : boolValue(value).toBool();
            hasResult = true;
        } else if (op == QLatin1String("isfalse")) {
            result = hasResult ? !result : !boolValue(value).toBool();
            hasResult = true;
        } else if (op.startsWith(QLatin1String("~="))) {
            result = containsText(value, op.mid(2).trimmed());
            hasResult = true;
        } else if (op.startsWith(u'~')) {
            result = containsText(value, op.mid(1).trimmed());
            hasResult = true;
        } else if (op.startsWith(u'=')) {
            result = equalsText(value, op.mid(1).trimmed());
            hasResult = true;
        } else if (op.startsWith(QLatin1String(">="))) {
            result = compareNumber(value, op.mid(2),
                                   [](double l, double r) { return l >= r; });
            hasResult = true;
        } else if (op.startsWith(QLatin1String("<="))) {
            result = compareNumber(value, op.mid(2),
                                   [](double l, double r) { return l <= r; });
            hasResult = true;
        } else if (op.startsWith(u'>')) {
            result = compareNumber(value, op.mid(1),
                                   [](double l, double r) { return l > r; });
            hasResult = true;
        } else if (op.startsWith(u'<')) {
            result = compareNumber(value, op.mid(1),
                                   [](double l, double r) { return l < r; });
            hasResult = true;
        }
    }
    return result;
}

QVariant StreamTemplateEngine::applyTransform(const QVariant& value,
                                              const QString& op) const
{
    if (op == QLatin1String("title")) return titleCased(valueToText(value));
    if (op == QLatin1String("lower"))
        return valueToText(value).toLower();
    if (op == QLatin1String("upper"))
        return valueToText(value).toUpper();
    if (op == QLatin1String("bytes")) {
        bool ok = false;
        const double n = numValue(value, &ok);
        return ok ? QVariant(formatBytes(n)) : QVariant(QString());
    }
    if (op == QLatin1String("time")) {
        bool ok = false;
        const double n = numValue(value, &ok);
        return ok ? QVariant(formatTime(n)) : QVariant(QString());
    }
    if (op.startsWith(QLatin1String("join("))) {
        const QStringList args = parseArgs(op);
        const QString sep = args.isEmpty() ? QStringLiteral(", ") : args[0];
        if (value.typeId() == QMetaType::QVariantList ||
            value.typeId() == QMetaType::QStringList) {
            QStringList parts;
            for (const QVariant& v : value.toList()) {
                const QString t = valueToText(v);
                if (!t.trimmed().isEmpty()) parts.append(t);
            }
            return parts.join(sep);
        }
        return valueToText(value);
    }
    if (op.startsWith(QLatin1String("replace("))) {
        const QStringList args = parseArgs(op);
        if (args.size() < 2) return valueToText(value);
        return valueToText(value).replace(args[0], args[1]);
    }
    return value;
}

int StreamTemplateEngine::findPlaceholderEnd(const QString& text, int start)
{
    QChar quote(0);
    for (int i = start; i < text.size(); ++i) {
        const QChar c = text[i];
        if (!quote.isNull()) {
            if (c == quote && (i == 0 || text[i - 1] != u'\\')) quote = QChar(0);
        } else if (c == u'\'' || c == u'"') {
            quote = c;
        } else if (c == u'}') {
            return i;
        }
    }
    return -1;
}

int StreamTemplateEngine::findTopLevelChar(const QString& text, QChar target)
{
    QChar quote(0);
    int depth = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (!quote.isNull()) {
            if (c == quote && (i == 0 || text[i - 1] != u'\\')) quote = QChar(0);
            continue;
        }
        if (c == u'\'' || c == u'"') quote = c;
        else if (c == u'(') ++depth;
        else if (c == u')') depth = std::max(0, depth - 1);
        else if (c == target && depth == 0) return i;
    }
    return -1;
}

QStringList StreamTemplateEngine::splitOps(const QString& text)
{
    QStringList tokens;
    QChar quote(0);
    int depth = 0;
    int start = 0;
    int i = 0;
    while (i < text.size()) {
        const QChar c = text[i];
        if (!quote.isNull()) {
            if (c == quote && text.mid(i - 1, 1) != QStringLiteral("\\"))
                quote = QChar(0);
            ++i;
            continue;
        }
        if (c == u'\'' || c == u'"') quote = c;
        else if (c == u'(') ++depth;
        else if (c == u')') depth = std::max(0, depth - 1);
        else if (c == u':' && depth == 0 && i + 1 < text.size() &&
                 text[i + 1] == u':') {
            tokens.append(text.mid(start, i - start).trimmed());
            i += 2;
            start = i;
            continue;
        }
        ++i;
    }
    tokens.append(text.mid(start).trimmed());
    QStringList out;
    for (const QString& t : tokens) {
        if (!t.isEmpty()) out.append(t);
    }
    return out;
}

QPair<QString, QString> StreamTemplateEngine::parseBranches(
    const QString& text)
{
    const int split = findBranchSeparator(text);
    if (split < 0) return qMakePair(parseQuoted(text), QString());
    return qMakePair(parseQuoted(text.left(split)),
                     parseQuoted(text.mid(split + 2)));
}

int StreamTemplateEngine::findBranchSeparator(const QString& text)
{
    QChar quote(0);
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (!quote.isNull()) {
            if (c == quote && text.mid(i - 1, 1) != QStringLiteral("\\"))
                quote = QChar(0);
            continue;
        }
        if (c == u'\'' || c == u'"') quote = c;
        else if (c == u'|' && i + 1 < text.size() && text[i + 1] == u'|')
            return i;
    }
    return -1;
}

QStringList StreamTemplateEngine::parseArgs(const QString& op)
{
    const int start = op.indexOf(u'(');
    const int end = op.lastIndexOf(u')');
    if (start < 0 || end <= start) return {};
    const QString body = op.mid(start + 1, end - start - 1);
    QStringList args;
    QChar quote(0);
    int argStart = 0;
    for (int i = 0; i < body.size(); ++i) {
        const QChar c = body[i];
        if (!quote.isNull()) {
            if (c == quote && body.mid(i - 1, 1) != QStringLiteral("\\"))
                quote = QChar(0);
            continue;
        }
        if (c == u'\'' || c == u'"') quote = c;
        else if (c == u',') {
            args.append(parseQuoted(body.mid(argStart, i - argStart)));
            argStart = i + 1;
        }
    }
    args.append(parseQuoted(body.mid(argStart)));
    return args;
}

QString StreamTemplateEngine::parseQuoted(const QString& raw)
{
    QString trimmed = raw.trimmed();
    if (trimmed.size() >= 2 &&
        ((trimmed.startsWith(u'"') && trimmed.endsWith(u'"')) ||
         (trimmed.startsWith(u'\'') && trimmed.endsWith(u'\''))))
        trimmed = trimmed.mid(1, trimmed.size() - 2);
    // Order matters (Compose parity): \n, \", \', \\ last.
    trimmed.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    trimmed.replace(QStringLiteral("\\\""), QStringLiteral("\""));
    trimmed.replace(QStringLiteral("\\'"), QStringLiteral("'"));
    trimmed.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    return trimmed;
}

bool StreamTemplateEngine::isFieldPath(const QString& token)
{
    return token.startsWith(QLatin1String("stream.")) ||
           token.startsWith(QLatin1String("service.")) ||
           token.startsWith(QLatin1String("addon."));
}

namespace {
[[nodiscard]] bool isTemplateBytes(const QVariant& value)
{
    return value.isValid() && !value.isNull() &&
           value.userType() == qMetaTypeId<TemplateBytes>();
}
} // namespace

bool StreamTemplateEngine::existsValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) return false;
    if (value.typeId() == QMetaType::QString)
        return !value.toString().trimmed().isEmpty();
    if (value.typeId() == QMetaType::QVariantList)
        return !value.toList().isEmpty();
    if (value.typeId() == QMetaType::QStringList)
        return !value.toStringList().isEmpty();
    return true;   // presence counts (truthiness is separate)
}

bool StreamTemplateEngine::isTruthy(const QVariant& value)
{
    // Compose `when` matches on TYPE: booleans/bytes/numbers first,
    // everything else (including numeric STRINGS like "0") falls to
    // exists(). A type-sniffing toDouble here would invert "0".
    if (value.typeId() == QMetaType::Bool) return value.toBool();
    if (isTemplateBytes(value))
        return value.value<TemplateBytes>().value != 0;
    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return value.toDouble() != 0.0;
    default:
        break;
    }
    return existsValue(value);
}

QVariant StreamTemplateEngine::boolValue(const QVariant& value)
{
    if (value.typeId() == QMetaType::Bool) return value;
    if (value.typeId() == QMetaType::QString) {
        const QString s = value.toString().trimmed().toLower();
        if (s == QLatin1String("true")) return QVariant(true);
        if (s == QLatin1String("false")) return QVariant(false);
    }
    return {};
}

double StreamTemplateEngine::numValue(const QVariant& value, bool* ok)
{
    if (isTemplateBytes(value)) {
        if (ok) *ok = true;
        return double(value.value<TemplateBytes>().value);
    }
    if (value.typeId() == QMetaType::Bool) {
        if (ok) *ok = false;
        return 0.0;
    }
    bool good = false;
    const double n = value.toDouble(&good);
    if (ok) *ok = good;
    return good ? n : 0.0;
}

bool StreamTemplateEngine::compareNumber(
    const QVariant& value, const QString& rawTarget,
    const std::function<bool(double, double)>& compare)
{
    bool ok = false;
    const double left = numValue(value, &ok);
    if (!ok) return false;
    bool okR = false;
    const double right = rawTarget.trimmed().toDouble(&okR);
    if (!okR) return false;
    return compare(left, right);
}

bool StreamTemplateEngine::equalsText(const QVariant& value,
                                      const QString& target)
{
    if (value.typeId() == QMetaType::QVariantList ||
        value.typeId() == QMetaType::QStringList) {
        for (const QVariant& v : value.toList()) {
            if (valueToText(v).trimmed().compare(
                    target, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    }
    return valueToText(value).trimmed().compare(target,
                                                Qt::CaseInsensitive) == 0;
}

bool StreamTemplateEngine::containsText(const QVariant& value,
                                        const QString& target)
{
    if (value.typeId() == QMetaType::QVariantList ||
        value.typeId() == QMetaType::QStringList) {
        for (const QVariant& v : value.toList()) {
            if (valueToText(v).contains(target, Qt::CaseInsensitive))
                return true;
        }
        return false;
    }
    return valueToText(value).contains(target, Qt::CaseInsensitive);
}

QString StreamTemplateEngine::valueToText(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) return {};
    if (value.typeId() == QMetaType::QVariantList ||
        value.typeId() == QMetaType::QStringList) {
        QStringList parts;
        for (const QVariant& v : value.toList()) {
            const QString t = valueToText(v);
            if (!t.trimmed().isEmpty()) parts.append(t);
        }
        return parts.join(QStringLiteral(", "));
    }
    if (isTemplateBytes(value))
        return formatBytes(double(value.value<TemplateBytes>().value));
    if (value.typeId() == QMetaType::Double ||
        value.typeId() == QMetaType::Float) {
        const double n = value.toDouble();
        if (std::floor(n) == n)
            return QString::number(qint64(n));
        return QString::number(n);
    }
    return value.toString();
}

QString StreamTemplateEngine::titleCased(const QString& s)
{
    const QStringList words = s.split(QRegularExpression(QStringLiteral("\\s+")));
    QStringList out;
    for (const QString& w : words) {
        if (w.trimmed().isEmpty()) {
            out.append(w);
            continue;
        }
        QString lower = w.toLower();
        lower[0] = lower[0].toUpper();
        out.append(lower);
    }
    return out.join(u' ');
}

QString StreamTemplateEngine::formatBytes(double value)
{
    const double bytes = std::fabs(value);
    if (bytes < 1024.0)
        return QStringLiteral("%1 B").arg(qint64(value));
    const char* units[] = {"KB", "MB", "GB", "TB"};
    double current = bytes;
    int unitIndex = -1;
    while (current >= 1024.0 && unitIndex < 3) {
        current /= 1024.0;
        ++unitIndex;
    }
    const double signedValue = value < 0 ? -current : current;
    if (signedValue >= 10 || std::floor(signedValue) == signedValue)
        return QStringLiteral("%1 %2").arg(qint64(signedValue))
            .arg(QLatin1String(units[unitIndex]));
    const qint64 tenths = qRound(signedValue * 10.0);
    return QStringLiteral("%1.%2 %3")
        .arg(tenths / 10)
        .arg(qint64(std::llabs(tenths % 10)))
        .arg(QLatin1String(units[unitIndex]));
}

QString StreamTemplateEngine::formatTime(double value)
{
    const qint64 seconds = qint64(value);
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 rest = seconds % 60;
    if (hours > 0)
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    if (minutes > 0)
        return QStringLiteral("%1m %2s").arg(minutes).arg(rest);
    return QStringLiteral("%1s").arg(rest);
}

} // namespace nuvio::debrid

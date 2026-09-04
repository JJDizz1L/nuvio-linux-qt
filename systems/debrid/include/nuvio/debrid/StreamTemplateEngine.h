#pragma once

// Stream template engine (D2): verbatim port of Compose
// DebridStreamTemplateEngine (placeholder {field::op[branches]||...},
// quote/paren-aware scanners, and/or condition groups, exists/istrue/
// isfalse/~/~/=/>=/<=/>/< ops, title/lower/upper/bytes/time/join/replace
// transforms, branch [true||false] rendering). Values are QVariantMaps
// (null/invalid = absent); lists render joined. Pure + headless-tested
// against the default name template shapes.

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>

#include <functional>

namespace nuvio::debrid {

/// Byte counts render through the bytes formatter (Compose parity).
struct TemplateBytes {
    qint64 value = 0;
};

class StreamTemplateEngine final {
public:
    [[nodiscard]] QString render(const QString& tmplate,
                                 const QVariantMap& values) const;

private:
    [[nodiscard]] QString renderExpression(const QString& expression,
                                           const QVariantMap& values) const;
    [[nodiscard]] bool evaluateCondition(const QString& expression,
                                         const QVariantMap& values) const;
    [[nodiscard]] bool evaluateSingle(const QVariant& value,
                                      const QStringList& ops) const;
    [[nodiscard]] QVariant applyTransform(const QVariant& value,
                                          const QString& op) const;
    [[nodiscard]] static int findPlaceholderEnd(const QString& text,
                                                int start);
    [[nodiscard]] static int findTopLevelChar(const QString& text, QChar target);
    [[nodiscard]] static QStringList splitOps(const QString& text);
    [[nodiscard]] static QPair<QString, QString> parseBranches(
        const QString& text);
    [[nodiscard]] static int findBranchSeparator(const QString& text);
    [[nodiscard]] static QStringList parseArgs(const QString& op);
    [[nodiscard]] static QString parseQuoted(const QString& raw);
    [[nodiscard]] static bool isFieldPath(const QString& token);
    [[nodiscard]] static bool existsValue(const QVariant& value);
    [[nodiscard]] static bool isTruthy(const QVariant& value);
    [[nodiscard]] static QVariant boolValue(const QVariant& value);
    [[nodiscard]] static double numValue(const QVariant& value,
                                         bool* ok = nullptr);
    [[nodiscard]] static bool compareNumber(
        const QVariant& value, const QString& rawTarget,
        const std::function<bool(double, double)>& compare);
    [[nodiscard]] static bool equalsText(const QVariant& value,
                                         const QString& target);
    [[nodiscard]] static bool containsText(const QVariant& value,
                                           const QString& target);
    [[nodiscard]] static QString valueToText(const QVariant& value);
    [[nodiscard]] static QString titleCased(const QString& s);
    [[nodiscard]] static QString formatBytes(double value);
    [[nodiscard]] static QString formatTime(double value);
};

} // namespace nuvio::debrid

Q_DECLARE_METATYPE(nuvio::debrid::TemplateBytes)

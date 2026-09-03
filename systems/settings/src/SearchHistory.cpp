#include "nuvio/settings/SearchHistory.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <memory>

#include "nuvio/settings/ActiveProfile.h"

#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::settings {

namespace {
[[nodiscard]] std::string keyName()
{
    return "search_history_" +
           std::to_string(ActiveProfile::id());
}
constexpr int  kMaxRecent = 10;
constexpr int  kMinLength = 2;
} // namespace

SearchHistory::SearchHistory(QObject* parent)
    : QObject(parent),
      m_store(std::make_unique<PropertiesStore>(
          PropertiesStore::defaultPath("search_history")))
{
}

SearchHistory::~SearchHistory() = default;

QStringList SearchHistory::decode(const QString& json)
{
    QStringList out;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray()) return out;   // Compose getOrNull() parity
    for (const auto& v : doc.array())
        if (v.isString()) out << v.toString();
    return out;
}

QString SearchHistory::encode(const QStringList& values)
{
    QJsonArray arr;
    for (const auto& v : values) arr.append(v);
    return QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QVariantList SearchHistory::recent()
{
    QVariantList out;
    for (const auto& v :
         decode(QString::fromStdString(
             m_store->getString(keyName()).value_or(""))))
        out.append(v);
    return out;
}

void SearchHistory::record(const QString& query)
{
    const QString q = query.trimmed();
    if (q.length() < kMinLength) return;

    QStringList cur =
        decode(QString::fromStdString(m_store->getString(keyName())
                                          .value_or("")));
    cur.removeAll(q);
    cur.prepend(q);                       // move-to-front dedupe
    while (cur.size() > kMaxRecent)
        cur.removeLast();

    persistLocked(cur);
    emit changed();
}

void SearchHistory::remove(const QString& query)
{
    QStringList cur =
        decode(QString::fromStdString(m_store->getString(keyName())
                                          .value_or("")));
    if (!cur.contains(query)) return;
    cur.removeAll(query);
    persistLocked(cur);
    emit changed();
}

void SearchHistory::clear()
{
    m_store->remove(keyName());
    m_store->persist();
    emit changed();
}

void SearchHistory::persistLocked(const QStringList& values)
{
    m_store->putString(keyName(), encode(values).toStdString());
    m_store->persist();
}

} // namespace nuvio::settings
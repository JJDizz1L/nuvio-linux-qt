#pragma once

// Recent-search history, byte-compatible with the Compose line's
// search_history.properties (key search_history_1, kotlinx List<String>
// payload, MOST-RECENT-FIRST order). Semantics ported verbatim from
// SearchHistoryRepository: trim, >=2 chars to record, move-to-front dedupe,
// hard cap 10 entries.

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

namespace nuvio::settings {

class PropertiesStore;

class SearchHistory final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList recent READ recent NOTIFY changed)

public:
    explicit SearchHistory(QObject* parent = nullptr);
    /// Out-of-line: unique_ptr over a fwd-declared store.
    ~SearchHistory() override;

    /// Most-recent-first; order-preserving read of the stored JSON list.
    [[nodiscard]] QVariantList recent();

    Q_INVOKABLE void record(const QString& query);
    Q_INVOKABLE void remove(const QString& query);
    Q_INVOKABLE void clear();

    // Pure codec (unit-tested): never throws on garbage -> empty list.
    [[nodiscard]] static QStringList decode(const QString& json);
    [[nodiscard]] static QString encode(const QStringList& values);

signals:
    void changed();

private:
    void persistLocked(const QStringList& values);

    std::unique_ptr<PropertiesStore> m_store;
};

} // namespace nuvio::settings
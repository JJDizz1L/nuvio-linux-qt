#pragma once

// User library (P5): verbatim port of Compose's library storage + sync
// shapes (StoredLibraryPayload: items/deltaCursorEventId/deltaInitialized/
// pendingUpsertKeys/pendingDeleteKeys; LibraryItem camelCase with unknown
// members preserved verbatim; item key "<lower-type>:<trim-id>"; snapshot
// sorted by savedAt desc on encode). Store file "library", profile-scoped
// key (live-proven). Sync RPCs live in LibrarySyncController (authsync).

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <optional>

namespace nuvio::library {

/// "<lowercased-type>:<trimmed-id>" (LibraryLocalState parity).
[[nodiscard]] QString libraryItemKey(const QString& id, const QString& type);

struct LibraryItem {
    QString id;
    QString type;
    QString name;
    QString poster;
    QString description;
    qint64 savedAtEpochMs = 0;
    /// Every other member (banner/logo/releaseInfo/imdbRating/genres/
    /// posterShape/addonBaseUrl/listKeys/trakt*/mediaCategory/tracking*...)
    /// rides verbatim so foreign writers never lose data here.
    QJsonObject extra;
};

struct LibrarySyncKey {
    QString contentId;
    QString contentType;
};

/// kotlinx-parity codec (encodeDefaults, null-omit on write, tolerant
/// decode, runCatching->empty on garbage).
class LibraryCodec final {
public:
    [[nodiscard]] static QList<LibraryItem> decodeItems(const QString& json);
    [[nodiscard]] static QString encodeItems(const QList<LibraryItem>& items);
};

/// Profile-scoped library repo (QObject for QML binding). Dirty tracking
/// (pending upserts/deletes) mirrors LibraryLocalState so the sync leg can
/// push exactly what changed locally.
class LibraryStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ itemsVariant NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    explicit LibraryStore(int profileId = 1, QObject* parent = nullptr);

    [[nodiscard]] QVariantList itemsVariant() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] QList<LibraryItem> items() const;

    Q_INVOKABLE bool isInLibrary(const QString& type,
                                 const QString& id) const;
    /// Adds (or refreshes the timestamp of) an entry; persists + notifies.
    Q_INVOKABLE void addToLibrary(const QString& type, const QString& id,
                                  const QString& name, const QString& poster,
                                  const QString& description,
                                  qint64 nowEpochMs);
    Q_INVOKABLE void removeFromLibrary(const QString& type, const QString& id);

    /// Full-payload access for the sync leg (envelope preserved).
    [[nodiscard]] QJsonObject loadPayload() const;
    void savePayload(const QJsonObject& payload);

    /// Sync-leg surface (dirty flags + envelope, no QML).
    [[nodiscard]] QList<LibrarySyncKey> pendingUpserts() const;
    [[nodiscard]] QList<LibrarySyncKey> pendingDeletes() const;
    [[nodiscard]] qint64 deltaCursorEventId() const;
    [[nodiscard]] bool deltaInitialized() const;
    void clearPendingUpserts(const QList<LibrarySyncKey>& keys);
    void clearPendingDeletes(const QList<LibrarySyncKey>& keys);
    void setDeltaCursor(qint64 eventId, bool initialized);
    /// Snapshot merge result (keeps pending flags; persists + notifies).
    void replaceItems(const QList<LibraryItem>& items);
    /// Delta merge (never dirties; persists + notifies when changed).
    void upsertRemoteItems(const QList<LibraryItem>& items);
    void removeRemoteKeys(const QList<LibrarySyncKey>& keys);

signals:
    void changed();

private:
    void load();
    void persist();

    int m_profileId;
    QList<LibraryItem> m_items;
    qint64 m_deltaCursorEventId = 0;
    bool m_deltaInitialized = false;
    QList<LibrarySyncKey> m_pendingUpserts;
    QList<LibrarySyncKey> m_pendingDeletes;
};

} // namespace nuvio::library

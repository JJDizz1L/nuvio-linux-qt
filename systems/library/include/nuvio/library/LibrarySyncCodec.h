#pragma once

// Library sync wire codec (P5): snake_case shapes verbatim from
// SupabaseLibrarySyncAdapter (LibrarySyncItem deltas/items, LibrarySyncKey
// deletes, bare-Long cursors). Pure + headless-tested; the controller owns
// transport.

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace nuvio::library {

struct LibraryItem;
struct LibrarySyncKey;

struct LibraryDeltaEvent {
    qint64 eventId = 0;
    QString operation;   // upsert | delete (case-tolerant at apply)
    QJsonObject item;    // raw wire object (decoded on apply)
};

class LibrarySyncCodec final {
public:
    /// Push shape (encodeDefaults parity: nullables emitted as null).
    [[nodiscard]] static QJsonObject toSyncItem(const LibraryItem& item);
    /// Wire object -> LibraryItem (added_at feeds savedAtEpochMs; all
    /// other wire members land in extra for verbatim fidelity).
    [[nodiscard]] static LibraryItem fromSyncItem(const QJsonObject& o);
    [[nodiscard]] static QJsonObject encodeKey(const LibrarySyncKey& key);
    [[nodiscard]] static QList<LibraryDeltaEvent> parseDeltaEvents(
        const QByteArray& body);
    /// Bare-number cursor bodies (QJsonDocument cannot hold scalars).
    [[nodiscard]] static qint64 parseCursor(const QByteArray& raw,
                                            qint64 fallback = 0);
    [[nodiscard]] static QJsonObject pushItemsParams(
        int profileId, const QList<QJsonObject>& items,
        const QString& originClientId);
    [[nodiscard]] static QJsonObject deleteItemsParams(
        int profileId, const QList<LibrarySyncKey>& keys,
        const QString& originClientId);
};

} // namespace nuvio::library

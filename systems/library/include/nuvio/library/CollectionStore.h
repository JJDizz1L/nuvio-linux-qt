#pragma once

// User collections (P5): Compose Collection/CollectionFolder shapes with
// byte-faithful unknown preservation. Only addon-catalog sources are
// modeled ({provider,addonId,type,catalogId,genre}); TMDB/Trakt sources
// and any other foreign members ride inside per-object raw bags so edits
// here never destroy them. Store file "collections", profile-scoped key
// (live Compose convention). Full-array export feeds the collections sync
// leg (sync_push_collections parity).

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

class QNetworkAccessManager;

namespace nuvio::library {

class AddonRegistry;

struct CollectionSource {
    QString provider = "addon";
    QString addonId;
    QString type;
    QString catalogId;
    QString genre;
};

struct CollectionFolder {
    QString id;
    QString title;
    QList<CollectionSource> addonSources;   // modeled subset
    QJsonArray otherSources;                // verbatim non-addon sources
    QJsonObject raw;                        // full folder object
};

struct Collection {
    QString id;
    QString title;
    bool pinToTop = false;
    QString viewMode = "TABBED_GRID";
    bool showAllTab = true;
    QList<CollectionFolder> folders;
    QJsonObject raw;                        // full collection object
};

/// kotlinx-parity array codec (ignoreUnknownKeys, tolerant decode).
class CollectionCodec final {
public:
    [[nodiscard]] static QList<Collection> decode(const QString& json);
    [[nodiscard]] static QString encode(const QList<Collection>& items);
};

class CollectionStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList collections READ collectionsVariant NOTIFY changed)
    /// Detail-routing selection (meta.load precedent): openCollection for
    /// the folder list, loadFolder (which also selects) for items.
    Q_PROPERTY(QVariantMap openCollection READ openCollectionVariant
                   NOTIFY opened)
    Q_INVOKABLE void openCollection(const QString& id);

public:
    explicit CollectionStore(int profileId = 1, QObject* parent = nullptr);

    /// Addon transport resolution for folder fetches (nullable in tests).
    void setAddonRegistry(AddonRegistry* registry);

    [[nodiscard]] QVariantList collectionsVariant() const;
    [[nodiscard]] QList<Collection> collections() const;
    [[nodiscard]] QVariantMap openCollectionVariant() const;

    /// Folder detail surface: sources of the loaded folder + fetched
    /// items (merged across sources; `folderSourceIndex` filters, -1 all).
    Q_PROPERTY(QVariantList folderSources READ folderSources
                   NOTIFY folderChanged)
    Q_PROPERTY(QVariantList folderItems READ folderItems NOTIFY folderChanged)
    Q_PROPERTY(QString folderTitle READ folderTitle NOTIFY folderChanged)
    Q_PROPERTY(int folderSourceIndex READ folderSourceIndex
                   WRITE setFolderSourceIndex NOTIFY folderChanged)
    Q_INVOKABLE void loadFolder(const QString& collectionId,
                                const QString& folderId);
    [[nodiscard]] QVariantList folderSources() const;
    [[nodiscard]] QVariantList folderItems() const;
    [[nodiscard]] QString folderTitle() const;
    [[nodiscard]] int folderSourceIndex() const { return m_folderSource; }
    void setFolderSourceIndex(int i);

    Q_INVOKABLE QString createCollection(const QString& title);
    Q_INVOKABLE void renameCollection(const QString& id, const QString& title);
    Q_INVOKABLE void removeCollection(const QString& id);
    Q_INVOKABLE void moveCollection(const QString& id, int delta);
    Q_INVOKABLE void setCollectionPinned(const QString& id, bool pinned);
    Q_INVOKABLE QString createFolder(const QString& collectionId,
                                     const QString& title);
    Q_INVOKABLE void renameFolder(const QString& collectionId,
                                  const QString& folderId,
                                  const QString& title);
    Q_INVOKABLE void removeFolder(const QString& collectionId,
                                  const QString& folderId);
    Q_INVOKABLE void addAddonSource(const QString& collectionId,
                                    const QString& folderId,
                                    const QString& addonId,
                                    const QString& type,
                                    const QString& catalogId,
                                    const QString& genre = QString());
    Q_INVOKABLE void removeAddonSource(const QString& collectionId,
                                       const QString& folderId, int index);

    /// Full-array export/import for the sync leg (verbatim fidelity).
    [[nodiscard]] QString exportToJson() const;
    void applyFromRemote(const QString& json);

signals:
    void changed();
    void folderChanged();
    void opened();

private:
    void load();
    void persist();
    void fetchFolderSource(const CollectionSource& source, quint64 token);
    [[nodiscard]] QVariantMap collectionVariant(const Collection& c) const;

    int m_profileId;
    QList<Collection> m_collections;
    QString m_openCollection;
    AddonRegistry* m_registry = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QString m_folderCollection;
    QString m_folderId;
    int m_folderSource = -1;
    QVariantList m_folderItems;
    quint64 m_folderToken = 0;
};

} // namespace nuvio::library

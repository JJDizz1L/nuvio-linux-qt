#include "nuvio/library/HomeShelves.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

#include "nuvio/library/AddonRegistry.h"
#include "nuvio/library/CatalogService.h"

namespace nuvio::library {

QVariantList applyReleaseFilter(const QVariantList& items, bool hideUnreleased,
                                const QString& todayIso)
{
    if (!hideUnreleased) return items;
    QVariantList out;
    for (const QVariant& v : items) {
        const QString released =
            v.toMap().value(QStringLiteral("released")).toString();
        // ISO dates compare lexicographically; empty/partial stays.
        if (released.size() >= 10 && released.left(10) > todayIso) continue;
        out.append(v);
    }
    return out;
}

QVariantList pickHeroItems(const QVariantList& sections, int limit)
{
    QVariantList out;
    for (const QVariant& s : sections) {
        if (out.size() >= limit) break;
        const QVariantList items =
            s.toMap().value(QStringLiteral("items")).toList();
        if (!items.isEmpty()) out.append(items.first());
    }
    return out;
}

HomeShelves::HomeShelves(AddonRegistry* registry, QObject* parent)
    : QObject(parent),
      m_registry(registry),
      m_nam(new QNetworkAccessManager(this))
{
    if (m_registry) {
        connect(m_registry, &AddonRegistry::changed, this,
                &HomeShelves::refresh);
    }
}

QVariantList HomeShelves::shelfPrefs() const
{
    QVariantList out;
    for (const HomeShelfPref& p : m_rows)
        out.append(QVariantMap{
            {QStringLiteral("key"), p.key},
            {QStringLiteral("title"),
             p.displayTitle(m_payload.showCatalogType)},
            {QStringLiteral("addonName"), p.addonName},
            {QStringLiteral("customTitle"), p.customTitle},
            {QStringLiteral("enabled"), p.enabled},
            {QStringLiteral("heroSourceEnabled"), p.heroSourceEnabled},
            {QStringLiteral("order"), p.order},
        });
    return out;
}

bool HomeShelves::heroEnabled() const { return m_payload.heroEnabled; }
bool HomeShelves::showCatalogType() const
{
    return m_payload.showCatalogType;
}
bool HomeShelves::hideUnreleasedContent() const
{
    return m_payload.hideUnreleasedContent;
}

void HomeShelves::setProfileId(int profileId)
{
    // The store has no id getter; re-target blindly (cheap) and refresh.
    m_store.setProfileId(profileId);
    emit prefsChanged();
    refresh();
}

void HomeShelves::refresh()
{    rebuildDefinitions();
    if (m_rows.isEmpty()) {
        m_sections.clear();
        m_hero.clear();
        m_queue.clear();
        emit sectionsChanged();
        return;
    }
    // Differential: prune vanished/disabled state, (re)fetch only keys
    // with no data and nothing in flight. A registry-changed ping during
    // an initial load therefore disturbs nothing already moving.
    QSet<QString> live;
    for (const HomeShelfPref& row : m_rows) {
        if (row.enabled) live.insert(row.key);
    }
    for (auto it = m_data.begin(); it != m_data.end();) {
        if (!live.contains(it.key()) || !m_defs.contains(it.key()))
            it = m_data.erase(it);
        else
            ++it;
    }
    for (const HomeShelfPref& row : m_rows) {
        if (!row.enabled) continue;
        auto it = m_data.find(row.key);
        if (it == m_data.end()) {
            ShelfData d;
            m_data.insert(row.key, d);
            if (!m_queue.contains(row.key)) m_queue.append(row.key);
        } else if (!it->done && !it->fetching &&
                   !m_queue.contains(row.key)) {
            m_queue.append(row.key);   // retry an abandoned key
        }
    }
    pumpQueue();
    recompute();
}

void HomeShelves::pumpQueue()
{
    while (m_pending < kMaxInFlight && !m_queue.isEmpty()) {
        const QString key = m_queue.takeFirst();
        auto it = m_data.find(key);
        if (it == m_data.end() || it->done || it->fetching) continue;
        fetchShelf(key);
    }
}

void HomeShelves::rebuildDefinitions()
{
    QList<HomeCatalogDefinition> defs;
    if (m_registry) {
        for (const QVariant& rv : m_registry->addons()) {
            const QVariantMap row = rv.toMap();
            if (row.value(QStringLiteral("enabled"), true) == false) continue;
            const QString manifestId = row.value(QStringLiteral("id")).toString();
            if (manifestId.isEmpty()) continue;   // placeholder, no manifest
            QString base = row.value(QStringLiteral("url")).toString();
            const QString suffix = QStringLiteral("/manifest.json");
            if (base.endsWith(suffix)) base.chop(suffix.size());
            for (const QVariant& cv :
                 row.value(QStringLiteral("catalogs")).toList()) {
                const QVariantMap c = cv.toMap();
                if (c.value(QStringLiteral("hasRequiredExtra")).toBool())
                    continue;
                const QString type = c.value(QStringLiteral("type")).toString();
                const QString cid = c.value(QStringLiteral("id")).toString();
                const QString cname =
                    c.value(QStringLiteral("name")).toString();
                if (type.isEmpty() || cid.isEmpty() || cname.isEmpty())
                    continue;
                HomeCatalogDefinition d;
                d.key = manifestId + u':' + type + u':' + cid;
                const QString typeLabel = type.left(1).toUpper() +
                                          type.mid(1).toLower();
                d.defaultTitle = cname + u' ' + typeLabel;
                d.addonName = row.value(QStringLiteral("name")).toString();
                d.addonId = manifestId;
                d.type = type;
                d.catalogId = cid;
                d.transportBase = base;
                defs.append(d);
            }
        }
    }
    m_payload = m_store.load();
    m_rows = m_store.reconcile(defs);
    m_defs.clear();
    for (const HomeCatalogDefinition& d : defs) m_defs.insert(d.key, d);
    // Drop fetched state for vanished/disabled keys.
    QSet<QString> live;
    for (const HomeShelfPref& r : m_rows) {
        if (r.enabled) live.insert(r.key);
    }
    for (auto it = m_data.begin(); it != m_data.end();) {
        if (!live.contains(it.key())) it = m_data.erase(it);
        else ++it;
    }
    emit prefsChanged();
}

void HomeShelves::recompute()
{
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    QVariantList sections;
    for (const HomeShelfPref& row : m_rows) {
        if (!row.enabled) continue;
        const auto it = m_data.find(row.key);
        QVariantList items;
        bool done = false;
        if (it != m_data.end()) {
            items = applyReleaseFilter(it->items,
                                       m_payload.hideUnreleasedContent, today);
            done = it->done;
        }
        sections.append(QVariantMap{
            {QStringLiteral("key"), row.key},
            {QStringLiteral("title"),
             row.displayTitle(m_payload.showCatalogType)},
            {QStringLiteral("subtitle"), row.addonName},
            {QStringLiteral("loading"), !done},
            {QStringLiteral("items"), items},
        });
    }
    m_sections = sections;
    if (m_payload.heroEnabled) {
        QVariantList heroSections;
        for (const QVariant& s : sections) {
            const QString key = s.toMap().value(QStringLiteral("key")).toString();
            for (const HomeShelfPref& r : m_rows) {
                if (r.key == key && r.heroSourceEnabled) {
                    heroSections.append(s);
                    break;
                }
            }
        }
        m_hero = pickHeroItems(heroSections);
    } else {
        m_hero.clear();
    }
    emit sectionsChanged();
}

void HomeShelves::fetchShelf(const QString& key)
{
    const auto defIt = m_defs.find(key);
    if (defIt == m_defs.end()) return;
    const HomeCatalogDefinition& def = *defIt;
    auto it = m_data.find(key);
    if (it == m_data.end() || it->fetching) return;
    it->fetching = true;
    ++m_pending;
    emit sectionsChanged();   // loading flags flip immediately
    const QUrl url(def.transportBase + QStringLiteral("/catalog/") +
                   def.type + QLatin1Char('/') + def.catalogId +
                   QStringLiteral(".json"));
    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("Accept", "application/json");
    QNetworkReply* rep = m_nam->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, rep, key] {
        rep->deleteLater();
        if (--m_pending < 0) m_pending = 0;
        auto it = m_data.find(key);
        if (it == m_data.end()) {
            pumpQueue();
            recompute();
            return;   // shelf vanished mid-flight: drop the body
        }
        it->fetching = false;
        it->done = true;
        if (rep->error() == QNetworkReply::NoError) {
            const QJsonArray metas = QJsonDocument::fromJson(rep->readAll())
                                         .object()
                                         .value(QLatin1String("metas"))
                                         .toArray();
            QVariantList items;
            for (const QJsonValue& mv : metas) {
                QVariantMap item = CatalogService::itemFromMeta(
                    mv.toObject().toVariantMap(), {});
                if (!item.isEmpty()) items.append(item);
            }
            it->items = items;
        }
        pumpQueue();   // free the slot before recomputing the view
        recompute();
    });
}

void HomeShelves::setHeroEnabled(bool on)
{
    if (m_payload.heroEnabled == on) return;
    m_payload.heroEnabled = on;
    m_store.save(m_payload);
    recompute();
    emit prefsChanged();
}

void HomeShelves::setShowCatalogType(bool on)
{
    if (m_payload.showCatalogType == on) return;
    m_payload.showCatalogType = on;
    m_store.save(m_payload);
    recompute();
    emit prefsChanged();
}

void HomeShelves::setHideUnreleasedContent(bool on)
{
    if (m_payload.hideUnreleasedContent == on) return;
    m_payload.hideUnreleasedContent = on;
    m_store.save(m_payload);
    recompute();
    emit prefsChanged();
}

void HomeShelves::setShelfEnabled(const QString& key, bool on)
{
    HomeCatalogPayload payload = m_store.load();
    bool found = false;
    for (HomeShelfPref& p : payload.items) {
        if (p.key == key) {
            p.enabled = on;
            found = true;
        }
    }
    if (!found) return;
    m_store.save(payload);
    m_payload = payload;
    // Reconcile against current rows to refresh the view (refetch when a
    // shelf comes back and has no data yet).
    for (HomeShelfPref& r : m_rows) {
        if (r.key == key) r.enabled = on;
    }
    if (on && !m_data.contains(key)) {
        ShelfData d;
        m_data.insert(key, d);
        if (!m_queue.contains(key)) m_queue.append(key);
        pumpQueue();
    }
    recompute();
    emit prefsChanged();
}

void HomeShelves::setShelfHeroSource(const QString& key, bool on)
{
    HomeCatalogPayload payload = m_store.load();
    bool found = false;
    for (HomeShelfPref& p : payload.items) {
        if (p.key == key) {
            p.heroSourceEnabled = on;
            found = true;
        }
    }
    if (!found) return;
    m_store.save(payload);
    m_payload = payload;
    for (HomeShelfPref& r : m_rows) {
        if (r.key == key) r.heroSourceEnabled = on;
    }
    recompute();
    emit prefsChanged();
}

void HomeShelves::setShelfCustomTitle(const QString& key, const QString& title)
{
    HomeCatalogPayload payload = m_store.load();
    bool found = false;
    for (HomeShelfPref& p : payload.items) {
        if (p.key == key) {
            p.customTitle = title.trimmed();
            found = true;
        }
    }
    if (!found) return;
    m_store.save(payload);
    m_payload = payload;
    for (HomeShelfPref& r : m_rows) {
        if (r.key == key) r.customTitle = title.trimmed();
    }
    recompute();
    emit prefsChanged();
}

void HomeShelves::moveShelf(const QString& key, int delta)
{
    if (delta == 0) return;
    HomeCatalogPayload payload = m_store.load();
    int at = -1;
    for (int i = 0; i < payload.items.size(); ++i) {
        if (payload.items[i].key == key) {
            at = i;
            break;
        }
    }
    if (at < 0) return;
    const int to = std::clamp(at + (delta > 0 ? 1 : -1), 0,
                              int(payload.items.size()) - 1);
    if (to == at) return;
    // Swap orders (stable even with sparse order values), then rebuild the
    // ordered view from the store.
    const int tmp = payload.items[at].order;
    payload.items[at].order = payload.items[to].order;
    payload.items[to].order = tmp;
    m_store.save(payload);
    rebuildDefinitions();
    recompute();
    emit prefsChanged();
}

} // namespace nuvio::library

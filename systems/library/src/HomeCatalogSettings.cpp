#include "nuvio/library/HomeCatalogSettings.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::library {

HomeCatalogPayload HomeCatalogSettingsCodec::decode(const QString& json)
{
    // Compose runCatching parity: garbage yields defaults. Presence checks
    // preserve the true-defaults (encodeDefaults always writes them, so
    // absent only happens on foreign/hand edits).
    HomeCatalogPayload out;
    const QJsonObject root =
        QJsonDocument::fromJson(json.toUtf8()).object();
    if (root.contains(QLatin1String("heroEnabled")))
        out.heroEnabled = root.value(QStringLiteral("heroEnabled")).toBool();
    if (root.contains(QLatin1String("showCatalogType")))
        out.showCatalogType =
            root.value(QStringLiteral("showCatalogType")).toBool();
    if (root.contains(QLatin1String("hideUnreleasedContent")))
        out.hideUnreleasedContent =
            root.value(QStringLiteral("hideUnreleasedContent")).toBool();
    for (const QJsonValue& v :
         root.value(QStringLiteral("items")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString key = o.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) continue;
        HomeShelfPref p;
        p.key = key;
        p.customTitle = o.value(QStringLiteral("customTitle")).toString();
        p.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        p.heroSourceEnabled =
            o.value(QStringLiteral("heroSourceEnabled")).toBool(true);
        p.order = o.value(QStringLiteral("order")).toInt();
        out.items.append(p);
    }
    return out;
}

QString HomeCatalogSettingsCodec::encode(const HomeCatalogPayload& payload)
{
    QJsonArray items;
    for (const HomeShelfPref& p : payload.items) {
        items.append(QJsonObject{
            {QStringLiteral("key"), p.key},
            {QStringLiteral("customTitle"), p.customTitle},
            {QStringLiteral("enabled"), p.enabled},
            {QStringLiteral("heroSourceEnabled"), p.heroSourceEnabled},
            {QStringLiteral("order"), p.order},
        });
    }
    const QJsonObject root{
        {QStringLiteral("heroEnabled"), payload.heroEnabled},
        {QStringLiteral("showCatalogType"), payload.showCatalogType},
        {QStringLiteral("hideUnreleasedContent"),
         payload.hideUnreleasedContent},
        {QStringLiteral("items"), items},
    };
    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Compact));
}

HomeCatalogSettingsStore::HomeCatalogSettingsStore(int profileId)
    : m_profileId(profileId)
{}

HomeCatalogPayload HomeCatalogSettingsStore::load() const
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(
            "home_catalog_settings"));
    const auto raw = store.getString("home_catalog_settings_" +
                                     std::to_string(m_profileId));
    if (!raw || raw->empty()) return {};
    return HomeCatalogSettingsCodec::decode(QString::fromStdString(*raw));
}

void HomeCatalogSettingsStore::save(const HomeCatalogPayload& payload)
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(
            "home_catalog_settings"));
    store.putString("home_catalog_settings_" + std::to_string(m_profileId),
                    HomeCatalogSettingsCodec::encode(payload).toStdString());
}

QList<HomeShelfPref> HomeCatalogSettingsStore::reconcile(
    const QList<HomeCatalogDefinition>& definitions)
{
    HomeCatalogPayload payload = load();
    QHash<QString, HomeShelfPref> known;
    int maxOrder = -1;
    for (const HomeShelfPref& p : payload.items) {
        known.insert(p.key, p);
        maxOrder = std::max(maxOrder, p.order);
    }
    bool added = false;
    QList<HomeShelfPref> rows;
    for (const HomeCatalogDefinition& d : definitions) {
        auto it = known.find(d.key);
        if (it == known.end()) {
            HomeShelfPref p;
            p.key = d.key;
            p.defaultTitle = d.defaultTitle;
            p.addonName = d.addonName;
            p.order = ++maxOrder;
            known.insert(d.key, p);
            added = true;
        } else {
            // Live titles/names follow the manifest, flags stay user's.
            it->defaultTitle = d.defaultTitle;
            it->addonName = d.addonName;
        }
        rows.append(known.value(d.key));
    }
    if (added) {
        payload.items = known.values();
        save(payload);
    }
    // Stable: stored files carry tied orders (Compose permits them);
    // definition order breaks ties deterministically run to run.
    std::stable_sort(rows.begin(), rows.end(),
                     [](const HomeShelfPref& a, const HomeShelfPref& b) {
                         return a.order < b.order;
                     });
    return rows;
}

} // namespace nuvio::library

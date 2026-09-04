#include "nuvio/deeplink/DeepLinkRouter.h"

#include "nuvio/deeplink/DeepLink.h"

namespace nuvio::deeplink {

void DeepLinkRouter::handleUrl(const QString& url)
{
    const QString normalized = url.trimmed();
    if (normalized.isEmpty() || !isAppUrl(normalized)) return;
    const DeepLink link = parseDeepLink(normalized);
    switch (link.kind) {
    case DeepLinkKind::Meta:
        emit openMeta(link.meta.type, link.meta.id);
        break;
    case DeepLinkKind::AddonInstall:
        emit installAddon(link.addon.manifestUrl);
        emit notice(tr("Checking addon…"));
        break;
    case DeepLinkKind::Downloads:
        emit openDownloads();
        break;
    case DeepLinkKind::None:
        break;
    }
}

} // namespace nuvio::deeplink

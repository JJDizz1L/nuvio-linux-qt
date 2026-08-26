#include "nuvio/ui/UiBootstrap.h"

#include "nuvio/ui/PlaceholderImageProvider.h"
#include "nuvio/mpv/MpvQuickItem.h"

#include <QQmlEngine>

namespace nuvio::ui {

void registerWith(QQmlEngine& engine)
{
    qmlRegisterType<nuvio::mpv::MpvQuickItem>("Nuvio.Mpv", 1, 0, "MpvItem");
    // Engine takes ownership of providers added this way.
    engine.addImageProvider(QStringLiteral("placeholder"),
                            new PlaceholderImageProvider);
}

} // namespace nuvio::ui
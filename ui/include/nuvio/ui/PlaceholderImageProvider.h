// Stand-in until the real Coil-parity provider (memory cap + disk cache)
// replaces it in P1. Exists so layout/binding code can already target a
// stable image source name instead of hardcoding local assets.
#pragma once

#include <QQuickImageProvider>

namespace nuvio::ui {

class PlaceholderImageProvider final : public QQuickImageProvider {
public:
    PlaceholderImageProvider();
    QPixmap requestPixmap(const QString& id, QSize* size,
                          const QSize& requestedSize) override;
};

} // namespace nuvio::ui

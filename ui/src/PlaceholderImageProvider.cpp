#include "nuvio/ui/PlaceholderImageProvider.h"

#include <QPainter>
#include <QPixmap>

namespace nuvio::ui {

PlaceholderImageProvider::PlaceholderImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

QPixmap PlaceholderImageProvider::requestPixmap(const QString& /*id*/,
                                                QSize* size,
                                                const QSize& requestedSize)
{
    const QSize s = requestedSize.isValid() ? requestedSize : QSize(16, 16);
    QPixmap pm(s);
    pm.fill(QColor(0x22, 0x24, 0x2b));                 // subtle neutral tile
    QPainter p(&pm);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x35, 0x39, 0x44));
    const int inset = std::max(1, s.width() / 12);
    p.drawRoundedRect(inset, inset, s.width() - 2 * inset,
                      s.height() - 2 * inset, inset * 2, inset * 2);
    if (size) *size = s;
    return pm;
}

} // namespace nuvio::ui

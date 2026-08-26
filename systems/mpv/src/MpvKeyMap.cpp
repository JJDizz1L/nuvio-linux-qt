#include "nuvio/mpv/MpvKeyMap.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QWheelEvent>

#include <QLatin1String>

namespace nuvio::mpv {

std::optional<QString> MpvKeyMap::textFor(const QKeyEvent* e)
{
    if (!e) return std::nullopt;

    const int k = e->key();
    if (k == Qt::Key_unknown || k == 0) return std::nullopt;

    // --- named keys ---------------------------------------------------------
    QString name;
    switch (k) {
        case Qt::Key_Space:          name = QStringLiteral("space"); break;
        case Qt::Key_Left:           name = QStringLiteral("left"); break;
        case Qt::Key_Right:          name = QStringLiteral("right"); break;
        case Qt::Key_Up:             name = QStringLiteral("up"); break;
        case Qt::Key_Down:           name = QStringLiteral("down"); break;
        case Qt::Key_PageUp:         name = QStringLiteral("pageup"); break;
        case Qt::Key_PageDown:       name = QStringLiteral("pagedown"); break;
        case Qt::Key_Home:           name = QStringLiteral("home"); break;
        case Qt::Key_End:            name = QStringLiteral("end"); break;
        case Qt::Key_Insert:         name = QStringLiteral("ins"); break;
        case Qt::Key_Delete:         name = QStringLiteral("del"); break;
        case Qt::Key_Backspace:      name = QStringLiteral("bs"); break;
        case Qt::Key_Escape:         name = QStringLiteral("esc"); break;
        case Qt::Key_Tab:            name = QStringLiteral("tab"); break;
        case Qt::Key_Return:
        case Qt::Key_Enter:          name = QStringLiteral("enter"); break;
        case Qt::Key_Minus:          name = QStringLiteral("-"); break;
        case Qt::Key_Equal:          name = QStringLiteral("="); break;
        case Qt::Key_BracketLeft:    name = QStringLiteral("["); break;
        case Qt::Key_BracketRight:   name = QStringLiteral("]"); break;
        case Qt::Key_Comma:          name = QStringLiteral(","); break;
        case Qt::Key_Period:         name = QStringLiteral("."); break;
        case Qt::Key_Slash:          name = QStringLiteral("/"); break;
        case Qt::Key_Semicolon:      name = QStringLiteral(";"); break;
        case Qt::Key_Apostrophe:     name = QStringLiteral("'"); break;
        case Qt::Key_Backslash:      name = QStringLiteral("\\"); break;
        case Qt::Key_QuoteLeft:      name = QStringLiteral("`"); break;
        case Qt::Key_MediaNext:      name = QStringLiteral("next"); break;
        case Qt::Key_MediaPrevious:  name = QStringLiteral("prev"); break;
        case Qt::Key_MediaPlay:      name = QStringLiteral("play"); break;
        case Qt::Key_MediaPause:     name = QStringLiteral("pause"); break;
        case Qt::Key_MediaTogglePlayPause: name = QStringLiteral("playpause"); break;
        case Qt::Key_MediaStop:      name = QStringLiteral("stop"); break;
        default: break;
    }

    if (name.isEmpty()) {
        const Qt::KeyboardModifiers m = e->modifiers();
        const bool keypad  = m.testFlag(Qt::KeypadModifier);
        const bool shift   = m.testFlag(Qt::ShiftModifier);

        if (k >= Qt::Key_A && k <= Qt::Key_Z) {
            const QChar ch(QLatin1Char(char('A' + (k - Qt::Key_A))));
            name = shift ? ch : ch.toLower();       // mpv: bare vs Shift+UPPER
        } else if (k >= Qt::Key_0 && k <= Qt::Key_9 && !keypad) {
            name = QString(QLatin1Char(char('0' + (k - Qt::Key_0))));
        } else if (keypad) {
            if (k >= Qt::Key_0 && k <= Qt::Key_9)   name = QStringLiteral("kp") + QString::number(k - Qt::Key_0);
            else switch (k) {
                case Qt::Key_Plus:   name = QStringLiteral("kp_add"); break;
                case Qt::Key_Minus:  name = QStringLiteral("kp_subtract"); break;
                case Qt::Key_Asterisk:name = QStringLiteral("kp_multiply"); break;
                case Qt::Key_Slash:  name = QStringLiteral("kp_divide"); break;
                case Qt::Key_Enter:  name = QStringLiteral("kp_enter"); break;
                case Qt::Key_Period: name = QStringLiteral("kp_decimal"); break;
                default: break;
            }
        } else if (shift && k >= Qt::Key_Exclam && k <= Qt::Key_ydiaeresis) {
            // Latin-1 printable shifted symbols land here; forward their UTF-8 char.
            const QChar ch(k);
            if (ch.isPrint()) name = QString(ch);
        }
        if (name.isEmpty()) return std::nullopt;
    }

    QString composed;
    const Qt::KeyboardModifiers m = e->modifiers();
    if (m.testFlag(Qt::ShiftModifier)) composed += QStringLiteral("Shift+");
    if (m.testFlag(Qt::ControlModifier)) composed += QStringLiteral("Ctrl+");
    if (m.testFlag(Qt::AltModifier)) composed += QStringLiteral("Alt+");
    if (m.testFlag(Qt::MetaModifier)) composed += QStringLiteral("Meta+");
    composed += name;
    return composed;
}

std::optional<QString> MpvKeyMap::textFor(const QWheelEvent* e)
{
    if (!e) return std::nullopt;
    const QPointF d = e->angleDelta();
    if (d.y() > 0)  return QString::fromLatin1(MpvWheel::kUp);
    if (d.y() < 0)  return QString::fromLatin1(MpvWheel::kDown);
    if (d.x() > 0)  return QString::fromLatin1(MpvWheel::kRight);
    if (d.x() < 0)  return QString::fromLatin1(MpvWheel::kLeft);
    return std::nullopt;
}

} // namespace nuvio::mpv

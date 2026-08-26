// Qt key-event -> mpv key-name translation for forwarding through the
// `keypress` command family (plan §7 keyboard ownership).
// Bounded subset mirroring mpv's input.c naming; anything unknown returns
// empty so the QML layer keeps it (fall-through doctrine).
#pragma once

#include <QString>
#include <optional>

QT_BEGIN_NAMESPACE
class QKeyEvent;
class QWheelEvent;
QT_END_NAMESPACE

namespace nuvio::mpv {

struct MpvWheel {
    static constexpr const char* kUp    = "WHEEL_UP";
    static constexpr const char* kDown  = "WHEEL_DOWN";
    static constexpr const char* kLeft  = "WHEEL_LEFT";
    static constexpr const char* kRight = "WHEEL_RIGHT";
};

class MpvKeyMap {
public:
    /** mpv key text incl. modifiers ("Shift+Right"), or empty -> fall through. */
    [[nodiscard]] static std::optional<QString> textFor(const QKeyEvent* e);

    /** Wheel direction token from a wheel event, or empty. */
    [[nodiscard]] static std::optional<QString> textFor(const QWheelEvent* e);

    /** Envelope names accepted by mpv's input command family. */
    static constexpr const char* kPress  = "keypress";
    static constexpr const char* kDown   = "keydown";
    static constexpr const char* kKeyUp  = "keyup";
};

} // namespace nuvio::mpv

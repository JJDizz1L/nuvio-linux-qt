#include "nuvio/ui/UiBootstrap.h"

#include "nuvio/ui/PlaceholderImageProvider.h"
#include "nuvio/mpv/MpvQuickItem.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QStringList>

namespace nuvio::ui {

namespace {
// Same faces Compose bundles (composeResources/font) and wires as its
// app-wide FontFamily; the family name is the one embedded in the TTFs.
constexpr const char* kAppFontFamily = "JetBrains Sans";
constexpr const char* kAppFontFiles[] = {
    ":/nuvio/assets/fonts/jetbrains_sans_regular.ttf",
    ":/nuvio/assets/fonts/jetbrains_sans_semibold.ttf",
    ":/nuvio/assets/fonts/jetbrains_sans_bold.ttf",
};
} // namespace

void registerWith(QQmlEngine& engine)
{
    qmlRegisterType<nuvio::mpv::MpvQuickItem>("Nuvio.Mpv", 1, 0, "MpvItem");
    // Engine takes ownership of providers added this way.
    engine.addImageProvider(QStringLiteral("placeholder"),
                            new PlaceholderImageProvider);
}

QString loadBundledFonts()
{
    QStringList families;
    for (const char* path : kAppFontFiles) {
        const int id = QFontDatabase::addApplicationFont(
            QString::fromLatin1(path));
        if (id < 0) continue;
        families += QFontDatabase::applicationFontFamilies(id);
    }
    if (families.isEmpty()) return {};
    const QString family = families.contains(QLatin1String(kAppFontFamily))
                               ? QLatin1String(kAppFontFamily)
                               : families.first();
    QGuiApplication::setFont(QFont(family));
    return family;
}

} // namespace nuvio::ui
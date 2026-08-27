#pragma once

// Typed preferences facade over PropertiesStore (plan §P1 "settings pages
// reading live from PropertiesStore"). Every access is persisted immediately;
// each key emits one granular change signal so bindings stay cheap.
//
// Keys are THE storage contract shared with the Compose line - match names
// byte-for-byte when porting a Compose profile value; never invent a second
// spelling of the same preference.

#include <QObject>

namespace nuvio::settings {

class AppSettings final : public QObject {
    Q_OBJECT
    // theme ------------------------------------------------------------------
    Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY darkThemeChanged)
    // playback ---------------------------------------------------------------
    Q_PROPERTY(QString decoderMode READ decoderMode WRITE setDecoderMode
                   NOTIFY decoderModeChanged)
    Q_PROPERTY(int cacheMb READ cacheMb WRITE setCacheMb NOTIFY cacheMbChanged)

public:
    explicit AppSettings(QObject* parent = nullptr);

    [[nodiscard]] bool    darkTheme() const;
    void                  setDarkTheme(bool v);
    [[nodiscard]] QString decoderMode() const;
    void                  setDecoderMode(const QString& v);
    [[nodiscard]] int     cacheMb() const;
    void                  setCacheMb(int v);

signals:
    void darkThemeChanged();
    void decoderModeChanged();
    void cacheMbChanged();

private:
    class Store;
    Store* m_store;   // PIMPL: keeps Qt-private includes out of the header
};

} // namespace nuvio::settings
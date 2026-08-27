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
    // torrent (store "torrent_settings", Compose-parity key "cache_size") ----
    Q_PROPERTY(QString torrentCacheSize READ torrentCacheSize
                   WRITE setTorrentCacheSize NOTIFY torrentCacheSizeChanged)
    // player track preferences ("settings" store; keys are Qt-line-local,
    // upstream Compose persists these inside an opaque blob - migrate at P4)
    Q_PROPERTY(QString preferredAudioLanguage READ preferredAudioLanguage
                   WRITE setPreferredAudioLanguage NOTIFY
                       preferredAudioLanguageChanged)
    Q_PROPERTY(QString preferredSubtitleLanguage READ preferredSubtitleLanguage
                   WRITE setPreferredSubtitleLanguage NOTIFY
                       preferredSubtitleLanguageChanged)
    Q_PROPERTY(bool useForcedSubtitles READ useForcedSubtitles
                   WRITE setUseForcedSubtitles NOTIFY useForcedSubtitlesChanged)

public:
    explicit AppSettings(QObject* parent = nullptr);

    [[nodiscard]] bool    darkTheme() const;
    void                  setDarkTheme(bool v);
    [[nodiscard]] QString decoderMode() const;
    void                  setDecoderMode(const QString& v);
    [[nodiscard]] int     cacheMb() const;
    void                  setCacheMb(int v);

    /// One of NONE | GB_2 | GB_5 | GB_10 (Compose P2pCacheSize enum names,
    /// default GB_2). Stored in the separate "torrent_settings" store.
    [[nodiscard]] QString torrentCacheSize() const;
    void                  setTorrentCacheSize(const QString& v);

    // Track-preference language selectors. Values are option words
    // ("device"/"original"/"none"/"forced") or normalized codes
    // (en, pt-BR, ...). Defaults mirror Compose: audio=device, subs=none.
    [[nodiscard]] QString preferredAudioLanguage() const;
    void setPreferredAudioLanguage(const QString& v);
    [[nodiscard]] QString preferredSubtitleLanguage() const;
    void setPreferredSubtitleLanguage(const QString& v);
    [[nodiscard]] bool    useForcedSubtitles() const;
    void                  setUseForcedSubtitles(bool v);

signals:
    void darkThemeChanged();
    void decoderModeChanged();
    void cacheMbChanged();
    void torrentCacheSizeChanged();
    void preferredAudioLanguageChanged();
    void preferredSubtitleLanguageChanged();
    void useForcedSubtitlesChanged();

private:
    class Store;
    Store* m_store;   // PIMPL: keeps Qt-private includes out of the header

    class TorrentStore;
    TorrentStore* m_torrentStore;
};

} // namespace nuvio::settings
#pragma once

// Typed preferences facade over PropertiesStore (plan §P1 "settings pages
// reading live from PropertiesStore"). Every access is persisted immediately;
// each key emits one granular change signal so bindings stay cheap.
//
// Keys are THE storage contract shared with the Compose line - match names
// byte-for-byte when porting a Compose profile value; never invent a second
// spelling of the same preference.
//
// P1a key coverage (ground truth: NuvioDesktop
// features/player/PlayerSettingsStorage.desktop.kt + PlayerSettingsRepository
// UiState defaults): every Linux-meaningful sync key is stored under the
// profile-scoped parity name in player_settings.properties. android_* / ios_*
// keys are skipped entirely (mobile-only; present-only export omits them and
// merge-apply ignores them - contract-safe). introdb_api_key is a credential:
// stored locally and accepted on remote-apply (mirroring Compose replace),
// but NEVER exported (mirroring Compose export + ProfileSettingsCredentialPolicy).

#include <QObject>
#include <QJsonObject>
#include <QStringList>

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
    Q_PROPERTY(QString secondaryPreferredAudioLanguage READ
                   secondaryPreferredAudioLanguage WRITE
                       setSecondaryPreferredAudioLanguage NOTIFY
                           preferredAudioLanguageChanged)
    Q_PROPERTY(QString preferredSubtitleLanguage READ preferredSubtitleLanguage
                   WRITE setPreferredSubtitleLanguage NOTIFY
                       preferredSubtitleLanguageChanged)
    Q_PROPERTY(QString secondaryPreferredSubtitleLanguage READ
                   secondaryPreferredSubtitleLanguage WRITE
                       setSecondaryPreferredSubtitleLanguage NOTIFY
                           preferredSubtitleLanguageChanged)
    Q_PROPERTY(bool useForcedSubtitles READ useForcedSubtitles
                   WRITE setUseForcedSubtitles NOTIFY useForcedSubtitlesChanged)
    Q_PROPERTY(bool subtitleStripSdh READ subtitleStripSdh WRITE
                   setSubtitleStripSdh NOTIFY subtitleStyleChanged)
    Q_PROPERTY(bool subtitleShowOnlyPreferredLanguages READ
                   subtitleShowOnlyPreferredLanguages WRITE
                       setSubtitleShowOnlyPreferredLanguages NOTIFY
                           subtitleStyleChanged)
    Q_PROPERTY(QString addonSubtitleStartupMode READ addonSubtitleStartupMode
                   WRITE setAddonSubtitleStartupMode NOTIFY subtitleStyleChanged)
    // integrations ("settings" store; Qt-line-local keys, blob parity = P4)
    Q_PROPERTY(bool discordEnabled READ discordEnabled WRITE setDiscordEnabled
                   NOTIFY discordEnabledChanged)

    // Subtitle appearance (Compose-parity keys; mpv-applied live).
    Q_PROPERTY(int subtitleFontSize READ subtitleFontSize WRITE
                   setSubtitleFontSize NOTIFY subtitleStyleChanged)
    Q_PROPERTY(QString subtitleTextColor READ subtitleTextColor WRITE
                   setSubtitleTextColor NOTIFY subtitleStyleChanged)
    Q_PROPERTY(QString subtitleBackgroundColor READ subtitleBackgroundColor
                   WRITE setSubtitleBackgroundColor NOTIFY subtitleStyleChanged)
    Q_PROPERTY(QString subtitleOutlineColor READ subtitleOutlineColor WRITE
                   setSubtitleOutlineColor NOTIFY subtitleStyleChanged)
    // Outline/bold/offset setters + getters predate the UI; register them
    // like the two above or QML reads yield undefined (dead switches/sliders).
    Q_PROPERTY(bool subtitleOutlineEnabled READ subtitleOutlineEnabled WRITE
                   setSubtitleOutlineEnabled NOTIFY subtitleStyleChanged)
    Q_PROPERTY(int subtitleOutlineWidth READ subtitleOutlineWidth WRITE
                   setSubtitleOutlineWidth NOTIFY subtitleStyleChanged)
    Q_PROPERTY(bool subtitleBold READ subtitleBold
                   WRITE setSubtitleBold NOTIFY subtitleStyleChanged)
    Q_PROPERTY(int subtitleBottomOffset READ subtitleBottomOffset WRITE
                   setSubtitleBottomOffset NOTIFY subtitleStyleChanged)

    // Player behavior (Compose-parity keys; behavior lands across P2/P3).
    Q_PROPERTY(bool showLoadingOverlay READ showLoadingOverlay WRITE
                   setShowLoadingOverlay NOTIFY playerOptionsChanged)
    Q_PROPERTY(bool showParentalGuide READ showParentalGuide WRITE
                   setShowParentalGuide NOTIFY playerOptionsChanged)
    Q_PROPERTY(QString resizeMode READ resizeMode WRITE setResizeMode NOTIFY
                   playerOptionsChanged)
    Q_PROPERTY(bool holdToSpeedEnabled READ holdToSpeedEnabled WRITE
                   setHoldToSpeedEnabled NOTIFY playerOptionsChanged)
    Q_PROPERTY(float holdToSpeedValue READ holdToSpeedValue WRITE
                   setHoldToSpeedValue NOTIFY playerOptionsChanged)
    Q_PROPERTY(bool touchGesturesEnabled READ touchGesturesEnabled WRITE
                   setTouchGesturesEnabled NOTIFY playerOptionsChanged)
    Q_PROPERTY(bool mapDv7ToHevc READ mapDv7ToHevc WRITE setMapDv7ToHevc
                   NOTIFY playerOptionsChanged)
    Q_PROPERTY(bool tunnelingEnabled READ tunnelingEnabled WRITE
                   setTunnelingEnabled NOTIFY playerOptionsChanged)
    Q_PROPERTY(bool useLibass READ useLibass WRITE setUseLibass NOTIFY
                   playerOptionsChanged)
    Q_PROPERTY(QString libassRenderType READ libassRenderType WRITE
                   setLibassRenderType NOTIFY playerOptionsChanged)
    Q_PROPERTY(bool nvidiaRtxSuperResolutionEnabled READ
                   nvidiaRtxSuperResolutionEnabled WRITE
                       setNvidiaRtxSuperResolutionEnabled NOTIFY
                           playerOptionsChanged)

    // External player (Compose-parity keys; launcher lands in P3).
    Q_PROPERTY(bool externalPlayerEnabled READ externalPlayerEnabled WRITE
                   setExternalPlayerEnabled NOTIFY playerOptionsChanged)
    Q_PROPERTY(bool externalPlayerForwardSubtitles READ
                   externalPlayerForwardSubtitles WRITE
                       setExternalPlayerForwardSubtitles NOTIFY
                           playerOptionsChanged)
    Q_PROPERTY(bool externalPlayerSendSkipSegments READ
                   externalPlayerSendSkipSegments WRITE
                       setExternalPlayerSendSkipSegments NOTIFY
                           playerOptionsChanged)
    Q_PROPERTY(QString externalPlayerId READ externalPlayerId WRITE
                   setExternalPlayerId NOTIFY playerOptionsChanged)

    // Stream reuse (Compose-parity keys).
    Q_PROPERTY(bool streamReuseLastLinkEnabled READ streamReuseLastLinkEnabled
                   WRITE setStreamReuseLastLinkEnabled NOTIFY
                       playerOptionsChanged)
    Q_PROPERTY(int streamReuseLastLinkCacheHours READ
                   streamReuseLastLinkCacheHours WRITE
                       setStreamReuseLastLinkCacheHours NOTIFY
                           playerOptionsChanged)

    // Poster hover preview (Qt-line-local keys; Compose keeps these inside
    // its opaque poster_card_style payload — no cross-line contract yet).
    Q_PROPERTY(bool hoverPreviewEnabled READ hoverPreviewEnabled WRITE
                   setHoverPreviewEnabled NOTIFY hoverPreviewChanged)
    Q_PROPERTY(int hoverPreviewDelayMs READ hoverPreviewDelayMs WRITE
                   setHoverPreviewDelayMs NOTIFY hoverPreviewChanged)

    // Stream autoplay (Compose StreamAutoPlayMode/Source enum-name strings).
    Q_PROPERTY(QString streamAutoPlayMode READ streamAutoPlayMode WRITE
                   setStreamAutoPlayMode NOTIFY streamAutoPlayChanged)
    Q_PROPERTY(QString streamAutoPlaySource READ streamAutoPlaySource WRITE
                   setStreamAutoPlaySource NOTIFY streamAutoPlayChanged)
    Q_PROPERTY(int streamAutoPlayTimeoutSeconds READ
                   streamAutoPlayTimeoutSeconds WRITE
                       setStreamAutoPlayTimeoutSeconds NOTIFY
                           streamAutoPlayChanged)
    Q_PROPERTY(QString streamAutoPlayRegex READ streamAutoPlayRegex WRITE
                   setStreamAutoPlayRegex NOTIFY streamAutoPlayChanged)
    Q_PROPERTY(QStringList streamAutoPlaySelectedAddons READ
                   streamAutoPlaySelectedAddons WRITE
                       setStreamAutoPlaySelectedAddons NOTIFY
                           streamAutoPlayChanged)
    Q_PROPERTY(QStringList streamAutoPlaySelectedPlugins READ
                   streamAutoPlaySelectedPlugins WRITE
                       setStreamAutoPlaySelectedPlugins NOTIFY
                           streamAutoPlayChanged)

    // Skip-intro / next-episode (Compose-parity keys; behavior lands in P3).
    Q_PROPERTY(bool skipIntroEnabled READ skipIntroEnabled WRITE
                   setSkipIntroEnabled NOTIFY playerOptionsChanged)
    Q_PROPERTY(QStringList autoSkipSegmentTypes READ autoSkipSegmentTypes
                   WRITE setAutoSkipSegmentTypes NOTIFY playerOptionsChanged)
    Q_PROPERTY(bool animeSkipEnabled READ animeSkipEnabled WRITE
                   setAnimeSkipEnabled NOTIFY playerOptionsChanged)
    Q_PROPERTY(QString animeSkipClientId READ animeSkipClientId WRITE
                   setAnimeSkipClientId NOTIFY playerOptionsChanged)
    Q_PROPERTY(QString introDbApiKey READ introDbApiKey WRITE setIntroDbApiKey
                   NOTIFY playerOptionsChanged)
    Q_PROPERTY(bool introSubmitEnabled READ introSubmitEnabled WRITE
                   setIntroSubmitEnabled NOTIFY playerOptionsChanged)
    Q_PROPERTY(bool streamAutoPlayNextEpisodeEnabled READ
                   streamAutoPlayNextEpisodeEnabled WRITE
                       setStreamAutoPlayNextEpisodeEnabled NOTIFY
                           playerOptionsChanged)
    Q_PROPERTY(bool streamAutoPlayNextEpisodeFallbackEnabled READ
                   streamAutoPlayNextEpisodeFallbackEnabled WRITE
                       setStreamAutoPlayNextEpisodeFallbackEnabled NOTIFY
                           playerOptionsChanged)
    Q_PROPERTY(bool streamAutoPlayPreferBingeGroup READ
                   streamAutoPlayPreferBingeGroup WRITE
                       setStreamAutoPlayPreferBingeGroup NOTIFY
                           playerOptionsChanged)
    Q_PROPERTY(bool streamAutoPlayReuseBingeGroup READ
                   streamAutoPlayReuseBingeGroup WRITE
                       setStreamAutoPlayReuseBingeGroup NOTIFY
                           playerOptionsChanged)
    Q_PROPERTY(QString nextEpisodeThresholdMode READ nextEpisodeThresholdMode
                   WRITE setNextEpisodeThresholdMode NOTIFY
                       playerOptionsChanged)
    Q_PROPERTY(float nextEpisodeThresholdPercent READ
                   nextEpisodeThresholdPercent WRITE
                       setNextEpisodeThresholdPercent NOTIFY
                           playerOptionsChanged)
    Q_PROPERTY(float nextEpisodeThresholdMinutesBeforeEnd READ
                   nextEpisodeThresholdMinutesBeforeEnd WRITE
                       setNextEpisodeThresholdMinutesBeforeEnd NOTIFY
                           playerOptionsChanged)

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
    // Secondary = "" when unset (Compose null; empty removes the key).
    [[nodiscard]] QString preferredAudioLanguage() const;
    void setPreferredAudioLanguage(const QString& v);
    [[nodiscard]] QString secondaryPreferredAudioLanguage() const;
    void setSecondaryPreferredAudioLanguage(const QString& v);
    [[nodiscard]] QString preferredSubtitleLanguage() const;
    void setPreferredSubtitleLanguage(const QString& v);
    [[nodiscard]] QString secondaryPreferredSubtitleLanguage() const;
    void setSecondaryPreferredSubtitleLanguage(const QString& v);
    [[nodiscard]] bool    useForcedSubtitles() const;
    void                  setUseForcedSubtitles(bool v);
    [[nodiscard]] bool    subtitleStripSdh() const;
    void                  setSubtitleStripSdh(bool v);
    [[nodiscard]] bool    subtitleShowOnlyPreferredLanguages() const;
    void                  setSubtitleShowOnlyPreferredLanguages(bool v);
    /// Addon-subtitle startup mode string (live Compose value FAST_STARTUP;
    /// no code default exists upstream - adopted from the shared file).
    [[nodiscard]] QString addonSubtitleStartupMode() const;
    void                  setAddonSubtitleStartupMode(const QString& v);
    [[nodiscard]] bool    discordEnabled() const;
    void                  setDiscordEnabled(bool v);

    // Subtitle appearance (desktop range 6..40sp; Compose default 18).
    // Colors are #AARRGGBB (Compose toStorageHexString; verified live).
    [[nodiscard]] int     subtitleFontSize() const;
    void                  setSubtitleFontSize(int v);
    [[nodiscard]] QString subtitleTextColor() const;   // "#RRGGBBAA"
    void                  setSubtitleTextColor(const QString& v);
    [[nodiscard]] QString subtitleBackgroundColor() const; // default #00000000
    void                  setSubtitleBackgroundColor(const QString& v);
    [[nodiscard]] QString subtitleOutlineColor() const;    // default #FF000000
    void                  setSubtitleOutlineColor(const QString& v);
    [[nodiscard]] bool    subtitleOutlineEnabled() const;
    void                  setSubtitleOutlineEnabled(bool v);
    [[nodiscard]] int     subtitleOutlineWidth() const;
    void                  setSubtitleOutlineWidth(int v);
    [[nodiscard]] bool    subtitleBold() const;
    void                  setSubtitleBold(bool v);
    [[nodiscard]] int     subtitleBottomOffset() const;
    void                  setSubtitleBottomOffset(int v);
    // Poster hover preview (Qt-line-local; defaults mirror Compose behavior).
    [[nodiscard]] bool    hoverPreviewEnabled() const;
    void                  setHoverPreviewEnabled(bool v);
    [[nodiscard]] int     hoverPreviewDelayMs() const;
    void                  setHoverPreviewDelayMs(int v);
    // Stream autoplay (enum-name strings, Compose defaults; source gains the
    // newer ENABLED_PLUGINS_ONLY value both Compose lines persist).
    [[nodiscard]] QString streamAutoPlayMode() const;
    void                  setStreamAutoPlayMode(const QString& v);
    [[nodiscard]] QString streamAutoPlaySource() const;
    void                  setStreamAutoPlaySource(const QString& v);
    [[nodiscard]] int     streamAutoPlayTimeoutSeconds() const;
    void                  setStreamAutoPlayTimeoutSeconds(int v);
    [[nodiscard]] QString streamAutoPlayRegex() const;
    void                  setStreamAutoPlayRegex(const QString& v);
    [[nodiscard]] QStringList streamAutoPlaySelectedAddons() const;
    void                  setStreamAutoPlaySelectedAddons(const QStringList& v);
    [[nodiscard]] QStringList streamAutoPlaySelectedPlugins() const;
    void                  setStreamAutoPlaySelectedPlugins(const QStringList& v);

    // Player behavior (Compose UiState defaults in comments).
    [[nodiscard]] bool    showLoadingOverlay() const;          // default true
    void                  setShowLoadingOverlay(bool v);
    [[nodiscard]] bool    showParentalGuide() const;           // default true
    void                  setShowParentalGuide(bool v);
    /// Fit | Fill | Zoom | Stretch (Compose PlayerResizeMode names).
    [[nodiscard]] QString resizeMode() const;                  // default Fit
    void                  setResizeMode(const QString& v);
    [[nodiscard]] bool    holdToSpeedEnabled() const;          // default true
    void                  setHoldToSpeedEnabled(bool v);
    [[nodiscard]] float   holdToSpeedValue() const;            // default 2.0
    void                  setHoldToSpeedValue(float v);
    [[nodiscard]] bool    touchGesturesEnabled() const;        // default true
    void                  setTouchGesturesEnabled(bool v);
    [[nodiscard]] bool    mapDv7ToHevc() const;                // default false
    void                  setMapDv7ToHevc(bool v);
    [[nodiscard]] bool    tunnelingEnabled() const;            // default false
    void                  setTunnelingEnabled(bool v);
    [[nodiscard]] bool    useLibass() const;                   // default false
    void                  setUseLibass(bool v);
    [[nodiscard]] QString libassRenderType() const;            // default CUES
    void                  setLibassRenderType(const QString& v);
    [[nodiscard]] bool    nvidiaRtxSuperResolutionEnabled() const; // false
    void                  setNvidiaRtxSuperResolutionEnabled(bool v);

    // External player (desktop default id "system").
    [[nodiscard]] bool    externalPlayerEnabled() const;       // default false
    void                  setExternalPlayerEnabled(bool v);
    [[nodiscard]] bool    externalPlayerForwardSubtitles() const; // false
    void                  setExternalPlayerForwardSubtitles(bool v);
    [[nodiscard]] bool    externalPlayerSendSkipSegments() const; // false
    void                  setExternalPlayerSendSkipSegments(bool v);
    [[nodiscard]] QString externalPlayerId() const;            // default system
    void                  setExternalPlayerId(const QString& v);

    // Stream reuse-link cache.
    [[nodiscard]] bool    streamReuseLastLinkEnabled() const;  // default false
    void                  setStreamReuseLastLinkEnabled(bool v);
    [[nodiscard]] int     streamReuseLastLinkCacheHours() const; // default 24
    void                  setStreamReuseLastLinkCacheHours(int v);

    // Skip-intro / next-episode (Compose defaults in comments).
    [[nodiscard]] bool    skipIntroEnabled() const;            // default true
    void                  setSkipIntroEnabled(bool v);
    /// Stored values are AutoSkipSegmentType storedValues
    /// (intro|recap|outro); sorted on write like Compose.
    [[nodiscard]] QStringList autoSkipSegmentTypes() const;    // default {}
    void                  setAutoSkipSegmentTypes(const QStringList& v);
    [[nodiscard]] bool    animeSkipEnabled() const;            // default false
    void                  setAnimeSkipEnabled(bool v);
    [[nodiscard]] QString animeSkipClientId() const;           // default ""
    void                  setAnimeSkipClientId(const QString& v);
    /// Credential: never exported (Compose credential policy), accepted on
    /// remote-apply like Compose replace.
    [[nodiscard]] QString introDbApiKey() const;               // default ""
    void                  setIntroDbApiKey(const QString& v);
    [[nodiscard]] bool    introSubmitEnabled() const;          // default false
    void                  setIntroSubmitEnabled(bool v);
    [[nodiscard]] bool    streamAutoPlayNextEpisodeEnabled() const; // false
    void                  setStreamAutoPlayNextEpisodeEnabled(bool v);
    [[nodiscard]] bool    streamAutoPlayNextEpisodeFallbackEnabled() const; // true
    void                  setStreamAutoPlayNextEpisodeFallbackEnabled(bool v);
    [[nodiscard]] bool    streamAutoPlayPreferBingeGroup() const;   // true
    void                  setStreamAutoPlayPreferBingeGroup(bool v);
    [[nodiscard]] bool    streamAutoPlayReuseBingeGroup() const;    // true
    void                  setStreamAutoPlayReuseBingeGroup(bool v);
    /// PERCENTAGE | MINUTES_BEFORE_END (Compose NextEpisodeThresholdMode).
    [[nodiscard]] QString nextEpisodeThresholdMode() const;    // PERCENTAGE
    void                  setNextEpisodeThresholdMode(const QString& v);
    [[nodiscard]] float   nextEpisodeThresholdPercent() const; // default 99
    void                  setNextEpisodeThresholdPercent(float v);
    [[nodiscard]] float   nextEpisodeThresholdMinutesBeforeEnd() const; // 2
    void                  setNextEpisodeThresholdMinutesBeforeEnd(float v);

    /// --- remote-profile-sync surface (leg 4) ---------------------------------
    /// Re-emits every change signal (profile switches change the keys
    /// underneath; QML bindings otherwise show the old profile's values).
    Q_INVOKABLE void refreshAll();
    /// Current player-settings feature payload (Compose blob v3 fragment).
    [[nodiscard]] QJsonObject exportPlayerSyncPayload();
    /// Applies a remote player_settings fragment through THIS instance's own
    /// stores (snapshot-safety: writing via a foreign PropertiesStore would
    /// be invisible to our cached views). Emits only the changed* signals
    /// whose values actually flipped. Returns true when anything changed.
    bool applyPlayerSyncPayload(const QJsonObject& payload);

signals:
    void darkThemeChanged();
    void subtitleStyleChanged();
    void hoverPreviewChanged();
    void streamAutoPlayChanged();
    void playerOptionsChanged();
    void decoderModeChanged();
    void cacheMbChanged();
    void torrentCacheSizeChanged();
    void preferredAudioLanguageChanged();
    void preferredSubtitleLanguageChanged();
    void useForcedSubtitlesChanged();
    void discordEnabledChanged();

private:
    class Store;
    Store* m_store;   // PIMPL: keeps Qt-private includes out of the header

    class TorrentStore;
    TorrentStore* m_torrentStore;
};

} // namespace nuvio::settings

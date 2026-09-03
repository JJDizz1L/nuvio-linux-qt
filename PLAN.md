# PLAN.md — Qt port completion (partials first)

Method: port in order P1→P7 below. Each phase: todo list → implement → full
`ctest` + offscreen boot check (+ smoke on player-touching phases) → report
DONE and wait for go. Backlog (Appendix A) is out of scope until P1–P7 land.
Skip list (Appendix B) is normative.

## P1 — Sync-blob breadth ✅ DONE 2026-09-03 (28/28 ctest, boot clean, smoke PASS advancing vaapi-copy)

- **P1a — full `player_settings` key coverage.** Every Linux-meaningful key
  from `PlayerSettingsStorage.desktop.kt` `syncKeys` (minus `android_*` /
  `ios_*`, never stored) now lives in `AppSettings` + `PlayerSettingsSync`
  under profile-scoped parity names with Compose `UiState`/`DEFAULT` values.
  Credential `introdb_api_key` stored + accepted on apply, never exported
  (Compose credential-policy parity). `ENABLED_PLUGINS_ONLY` source value,
  `Int.MAX_VALUE` timeout ("no timeout"), `FAST_STARTUP` default adopted
  from the live shared file. `useForcedSubtitles` absent-default flipped
  true→false (Compose `SubtitleStyleState.DEFAULT`; both live files store
  explicit true so no live profile changes). Applier now drives
  `sub-back-color` + `sub-outline-color` (mpv option names verified on-box);
  orchestrator observes `subtitleStyleChanged`/`streamAutoPlayChanged`/
  new `playerOptionsChanged`.
- **P1b — full-fidelity blob.** `SyncBlobFeatures` + `BlobPassthroughStore`:
  read-modify-write passthrough of all 13 unowned features (verbatim cache,
  merge-only, never fabricate/forward `""`/`{}`) — safe under replace AND
  merge server semantics (Compose `applyRemoteBlob` writes blindly, so a
  partial push would otherwise wipe sibling theme/payload/badge state).
  CW payload string applies verbatim into the shared CW store
  (`loadRaw`/`saveRaw` + `WatchRecorder::reloadContinueWatchingPrefs`) and
  re-sends on push; pushes gated until the first pull attempt completes;
  `cwPrefsChanged` schedules blob pushes (wired in `main.cpp`).
- Gotchas proven en route: Qt `QJsonDocument` rejects top-level scalars
  (array-wrap parse); hand-escaped JSON test literals rot (assemble with
  `QJsonDocument`); two orchestrators pushing in one 200 ms window lose a
  reply in the single-threaded TCP fake (test uses an isolated pair now).

## P2 — Settings split into pages (NEXT)

`ui/qml/pages/Settings/` root + leaves (Playback, Streams, Appearance,
ContinueWatching, HoverPreview, Tracking, Tmdb, MdbList, Notifications,
Integrations, Addons, Account). Keep `SettingsPage.qml` as forwarder during
migration. Surface the P1a keys (sub bg/outline colors, strip-SDH,
show-only-preferred, secondaries, resize, hold-to-speed, external player,
reuse-link, autoplay sets + `ENABLED_PLUGINS_ONLY`, skip/next-episode
family, libass, RTX key). Offscreen boot check per new QML file.

## P3 — Player depth

Next-episode autoplay continuation (threshold mode/percent/minutes);
subtitle engine (SDH strip, show-only-preferred); resize mode;
hold-to-speed; loading overlay; parental-guide gate; skip-intro/AnimeSkip/
IntroDb client + button + submit; external-player launcher; `StreamsPanel`/
`NextEpisodePopup`; binge-group/reuse linkage. Keytest 4/4 + smoke PASS.

## P4 — Home depth

Hero + catalog rails + discover row on `HomePage.qml` (CW wide-card
untouched); homescreen settings leaf drives visibility/order; 4K
scene-graph hygiene (no per-frame JS in delegates).

## P5 — Library depth

`LibraryStore` + `CollectionStore` (Compose shapes, profile-scoped);
My-Library + Collections sections; collection editor QML; library/
collection sync adapters (full-then-delta family pattern). Unblocks
`collection_mobile_settings` blob value.

## P6 — Details depth

Cast/comments/ratings/production/poster-rails in `MetaService` + `MetaPage`;
`PersonPage` + `EntityBrowsePage` routes in `NavigationModel`; season modes.

## P7 — Profiles full switcher (last partial)

Plumb `activeProfileId` (replace hardcoded 1); selection/edit/PIN/avatar/
switcher UI; migrate all stores to `<base>_<id>`; profile-id params on sync
controllers (server ids 1–6 only); device-link + server-discovery (re-verify
names in Desktop first — earlier inventory only).

## Appendix A — Not-started backlog (do NOT execute in this plan)

Trakt / Simkl / tracking abstraction → provider-creds sync; Debrid
(Torbox/Premiumize/RealDebrid + template engine) + cloud library; Downloads;
episode-release notifications; TMDB service + settings; MDBList;
membership/supporter; plugins runtime (QuickJS/WASM/DOM/crypto);
library/collection/home-catalog sync legs; in-app updater; deeplink
(`nuvio://`/`stremio://`); Sentry; P5 packaging (DEB/RPM/Flatpak/AppImage,
portal helper, signing, AUR).

## Appendix B — Skip / incompatible (normative)

- Direct `.kt`/Compose import: impossible by toolchain (re-express only).
- `androidMain`/`iosMain`/Windows/macOS actuals, `android_*`/`ios_*` keys:
  skip by Linux-target scope.
- JVM packaging + Skiko/AWT workarounds: deliberately dead (replaced by
  distro-Qt + `QQuickFramebufferObject`).
- Contract divergences by design: `vaapi`/`nvdec` pins → `auto`;
  `theme_dark` Qt-local (no Compose bool); unowned blob keys omitted, never
  `{}`/`""`; profile ids server-validated 1–6.

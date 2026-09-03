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

## P2 — Settings split into pages ✅ DONE 2026-09-03 (28/28 ctest, boot clean zero QML errors)

Flat `pages/Settings<Leaf>Page.qml` leaves on `settings-<leaf>` routes
(string stack — no C++ change): Appearance (theme/hover/overlay/parental/
resize), Playback (decoder/cache/hold-to-speed/external/reuse/libass +
synced-but-inert flags under an honest caption), Subtitles & tracks
(style + bg/outline colors + SDH/show-only/forced + primary/secondary
langs + startup mode), Streams & autoplay (mode/source incl.
`ENABLED_PLUGINS_ONLY`/timeout incl. No-timeout/regex + scoped-set counts
with clear + next-episode + skip-intro/AnimeSkip/IntroDb), Continue
Watching (moved verbatim), Integrations (discord + torrent), Account
(status + sign out / go-to-sign-in). `SettingsPage.qml` is the root list;
every leaf `ScrollView`s (the old single page overflowed 720p); all leaves
instantiate in `MainShell` so load-time boot checks cover them. Timeout
combo display subset narrowed (stored values still snap full-range in C++).
Tracking/TMDB/MDBList/notifications leaves deliberately absent — no
backends yet (P3+ with their features).

## P3 — Player depth ✅ DONE 2026-09-03 (32/32 ctest, boot clean, keytest 4/4, smoke PASS vaapi-copy)

- **Behaviors.** `NextEpisodeRules` pure (ordered continuation, threshold/
  outro card with Compose clamps, aired compare, tt/composite helpers) +
  `NextEpisodeHelper` QML bridge (`nextep`); VideoPage card with 3-2-1
  countdown when auto-play is on, manual Play otherwise; episode-list
  snapshot from the shell (hover-proof). Reuse-link cache
  (`StreamLinkCache`: contentKey/hash/credential-table/freshness verbatim,
  profile-suffixed keys live-proven) wired into `PlaybackSession` (fast
  path + refresh on direct; torrent relays never cached) + S/E props.
  Resize via verbatim bridge mapping in the applier; hold-to-speed
  (`MpvQuickItem::setSpeed` + transport hold button, restores 1.0 on
  release/leave); loading overlay on `buffering`; parental guide
  (`ParentalGuideResolver`, tiffara, dominant-severity + severe-first top-5,
  once-per-session card).
- **Subtitle engine.** `sub-filter-sdh` + `harder` from the strip pref
  (verified on-box); Subs menu show-only-preferred display filter
  (preferred + selected stay; exact/region-prefix subset documented —
  full alias tables stay in the C++ auto-selector).
- **Skip-intro.** `SkipResolver` (IntroDb configurable via
  `NUVIO_INTRODB_URL`, blank-disabled like Compose; AniSkip by MAL id;
  kitsu:/anilist need Simkl — honest empty; category-priority merge;
  per-key cache; single-flight + 15 s partial guard; submit with Bearer
  key); VideoPage button + pump auto-skip (category-normalized, once per
  segment) + submit dialog (needs switch + key; key never syncs).
- **External + StreamsPanel.** TransportBar External button (direct urls
  only — `currentIsLocalRelay` gates localhost relays; forwarding/skip
  handoff deferred); `StreamsPanel` (resolver `allStreams` + `addonName`,
  Compose empty-set-means-all scope checkboxes normalizing back to empty,
  future-resolutions-only note).
- Simplifications recorded: continuation reuses the best-pick resolver
  (no binge-group data yet — P4); no released dates ride episode rows so
  the aired gate treats unknown as aired (Compose default).
- TEST-LATER (user-deferred visual/live passes): skip fetch end-to-end
  (real anime id + network at watch time), parental fetch card, reuse-hit
  playback, next-episode popup + countdown, hold-to-speed feel, StreamsPanel
  scope edits affecting a later autoplay pick.

## P4 — Home depth ✅ DONE 2026-09-03 (33/33 ctest incl. new nuvio_home, boot clean, smoke PASS vaapi-copy)

- **Shelves.** `HomeCatalogSettings` codec/store (verbatim payload JSON,
  `home_catalog_settings_1`, reconcile keeps user flags + appends new keys,
  stable sort for tied orders) + `HomeShelves` fetcher (definitions from
  enabled manifests, required-extra skipped, `/catalog` fetch, release
  filter via carried `released` dates, deterministic hero = first items of
  ≤2 hero sources). `parseManifest` rows gained `catalogs`; `itemFromMeta`
  gained `type`/`description`/`released` (additive).
- **UI.** HomePage = hero spotlight (poster/title/meta/Play/Info) + CW
  rail (verbatim) + addon rails (library card idiom, watched badges);
  new `SettingsHomescreenPage` leaf (hero/type/unreleased switches +
  per-shelf enable/hero-source/custom-title/order) + root entry.
- **Storm fix (bisect-proven).** First cut fired ~62 parallel fetches and
  re-invalidated everything on each registry ping; the UI-thread rebuild
  churn starved item-snapshot delivery (controller cache advanced,
  advances=0 — the P0 signature). Fixed with differential refresh
  (prune + fetch-missing only), cap-4 fetch queue, per-key fetching
  state (global token removed). P3 tree passed mid-bisect, fixed tree
  passes.
- Divergences: hero pick deterministic (Compose seeded-random); non-tt
  catalog rows dropped (MetaService detail is tt-only); no separate
  discover row (addon rails cover it; search-discover stays a later port);
  LibraryPage untouched (unification is P5 business).

## P5 — Library depth ✅ DONE 2026-09-03 (36/36 ctest, boot clean, smoke PASS vaapi-copy)

- **Stores.** `LibraryStore` (verbatim payload + `library_1`, item key
  `<type>:<id>`, unknown members preserved, dirty upsert/delete flags,
  envelope API mirroring WatchingStore) + `CollectionStore` (verbatim
  collections array, TMDB/Trakt sources + foreign members preserved
  through raw bags, full CRUD + folder/source editing, export/apply).
- **Sync.** `LibrarySyncController` (offset-paged snapshot + legacy
  migration + bare-Long cursor + delta with pending-wins + dirty
  push/delete legs, batch-500 constants) + `CollectionSyncController`
  (full pull replace + debounced full push). Wired in main.cpp
  (change signals + session-activation pulls).
- **UI.** LibraryPage gains My Library rail + Collections rail (Manage
  button); MetaPage movie + series library toggles; `CollectionsPage`
  manager (collections/folders/sources incl. addon-catalog picker);
  `CollectionDetailPage` + `CollectionFolderPage` (source picker +
  merged grid); routes `collections`/`collectiondetail`/
  `collectionfolder`.
- Found en route: re-add refreshed only the timestamp (names/posters
  stuck) — now refreshes display fields too.
- Deferred: display-settings sort/layout (no backend yet); hover
  previews on the new rails (library cinemeta rails keep theirs);
  `collection_mobile_settings` stays blob-passthrough (no mobile UI).

## P6 — Details depth ✅ DONE 2026-09-03 (36/36 ctest, boot clean, smoke PASS vaapi-copy)

- **Parse.** `metaFromJson` gains director/writer/awards/country/imdbLink
  (links[] category match); `seasonViewMode` property on MetaService
  (posters default, toggle, profile-scoped `season_view_mode_1` parity).
- **UI.** MetaPage: crew lines, awards/country line, cast rail
  (display-only chips), IMDb button, season Posters/Text toggle with
  compact text rows.
- Deferred with reasons (no guessing): comments (Trakt backend),
  person detail (TMDB-backed), entity browse (TMDB), more-like-this
  (TMDB/TRAKT sources only), production companies/networks (TMDB
  enrichment), external ratings beyond IMDb (Trakt/MDBList). All ride
  the tracking/TMDB backlog, not this phase.

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

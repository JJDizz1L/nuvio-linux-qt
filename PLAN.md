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

## P7 — Profiles full switcher (last partial) ✅ DONE 2026-09-03 (38/38 ctest, boot clean, smoke PASS vaapi-copy)

- **Plumbing.** `ActiveProfile` process id (1..6, default 1) replaces every
  hardcoded profile constant: AppSettings/SyncPlayerSettings/SearchHistory/
  season-view/addon truth/link-cache key builders read it per access (no
  reload needed); Watching/Library/Collection/CW-prefs/home-catalog
  stores, addon registry, all six sync controllers and the passthrough
  cache (now profile-suffixed) expose setProfileId; AuthService persists
  GoTrue `user.id` (+ getter, cleared on sign-out).
- **Module.** `systems/profiles`: verbatim payload/NuvioProfile/push/lock
  shapes, `sha256("profile:<i>:<salt>:<pin>")` + ULong-hex salts,
  per-index salted PIN cache; `ProfileManager` (local payload with
  account-mismatch reset, server pull/push(sorted)/create/update/delete,
  online PIN RPC + offline cache verify with updatedAt staleness,
  lockout enforcement, in-session verified set, anonymous-local CRUD).
- **UI + wiring.** `ProfilesPage` route (cards, inline PIN sheet, editor,
  create, 6-cap) + Account switcher section; main.cpp manager, context
  prop, 13-target reload fan-out, session-activation pulls, first-run
  seeding to `profiles` when several exist and none was ever picked.
- Deferred with reasons: device-link QR/code login + custom-server
  discovery/switch (new auth flows + server-config storage, Appendix A);
  avatar catalog fetch (server bucket; color-hex picker ships, ids/urls
  pass through); `rememberLastProfile` honored in payload only (no
  auto-resume UX yet).

## Appendix A — backlog (P1–P7 landed; A1 done, rest ordered)

### A1 Tracking (Trakt/SIMKL) ✅ DONE 2026-09-03 (42/42 ctest, boot clean, smoke PASS vaapi-copy)

- **T1 abstraction.** `systems/tracking`: provider ids/capabilities,
  media reference (Compose MOVIE/SHOW/ANIME kinds — not Movie/Episode),
  wire actions, registry (auth-pushed connected set, supervisor fan-out),
  coordinator (active-profile guard), `ScrobblePump` (start-once,
  pause-edge, 80% completion stop, >5 s-jump seek stop+restart); wired to
  session begin + VideoPage 1 Hz tick; silent with nothing connected.
- **T2 Trakt.** Device-code auth (code/poll/redeem/refresh/invalidate,
  per-profile `trakt_auth_payload_<id>` + legacy migration, env creds,
  inert when empty) + scrobbler (verbatim bodies with omitted nulls,
  8 s/±1.5 throttle, stop-after-start exemption, stop retried 2x,
  401 signs out) registered STOP_AND_RESTART.
- **T3 SIMKL.** PIN auth (code/poll/outcomes, per-profile token store) +
  direct scrobbler (verbatim DTOs incl. tv-style-anime show leg, 2dp
  progress); PKCE browser flow needs deeplinks (deferred); full sync
  engine (snapshots/projections/playback merge) stays in backlog.
- **T4 creds + UI.** `SettingsTrackingPage` (both provider cards, codes,
  cancel, sign-out, connected line) + `ProviderCredsController`
  (animeskip/introdb through push/seed/pull, remote-wins merge, baseline
  dedup). **Parity fix:** the blob export leaked both credential keys —
  Compose strips them (credential policy); export omits them now, the
  credential family is their only wire path.
- Deferred with reasons: Trakt/SIMKL library/progress/watched providers
  + comments/related (need the read backends + engine), list/history
  writers, device-link QR login, custom-server discovery, avatar catalog
  fetch, Trakt OAuth browser callback (needs deeplinks).
- TEST-LATER: device/PIN flows against real accounts (user at
  trakt.tv/activate, simkl.com/pin), live scrobble round-trip,
  credential vault round-trip on the Tier-1 pattern.

Remaining, in suggested order:
packaging
cutover (DEB/RPM/Flatpak/AppImage, portal helper, signing, AUR).

### A2 Debrid ✅ DONE 2026-09-03 (47/47 ctest, boot clean, smoke PASS vaapi-copy)

- **D1 providers.** `systems/debrid`: verbatim provider table, full
  `debrid_settings` key set with Compose defaults (incl. verbatim
  template defaults), per-provider API clients (Torbox/Premiumize/RD
  URLs, bodies, envelope parses, Bearer auth), device-code auth
  (Torbox/Premiumize) + API-key validate, per-profile key storage.
- **D2 unrestrict.** Template engine verbatim (incl. type-truthiness
  subtleties caught by tests) + file selectors (name normalization,
  pattern triple, rule chain) + `DebridResolver` (credential pick,
  cache-check, multipart create, file select, download links;
  RD never auto-resolves) wired as a session tier between direct and
  P2P + stream values/formatter + debrid_settings blob fragment
  (credentials stripped at assembly).
- **D3 cloud + UI.** `CloudLibrary` (verbatim Torbox/Premiumize list
  mappings, grouping, playable tables, per-type playback resolve) +
  `SettingsDebridPage` (provider cards, resolver prefs, templates) +
  `CloudPage` browser (refresh, files, play) + routes + shell play
  routing (no watch session, trailer precedent).
- **Interop fix (bisect-grade):** the blob carried profile-scoped
  (`_1`) key names; Compose uses BARE keys on the wire (stores are
  scoped). Player fragment fixed + suites converted; cross-line blob
  round-trip stays TEST-LATER.
- Deferred: metadata facts backfill (resolution/codec/tags for
  templates), list sort/filter shaping, TMDB/Trakt collection sources
  in the editor (preserved, not creatable), episode patterns beyond
  SxxEyy in selectors.
- TEST-LATER: real provider keys end-to-end (unrestrict, cloud
  browse/play), device flows against live accounts, cross-line blob
  round-trip Compose↔Qt.

### A3 Downloads manager ✅ DONE 2026-09-03 (48/48 ctest, boot clean zero QML errors, smoke PASS vaapi-copy)

- **DL1 engine.** `systems/downloads`: verbatim `DownloadItem`
  (camelCase JSON, `logicalContentKey`, `isEpisode`, `isPlayable`,
  `progressFraction`), `DownloadStatus` names, profile store file
  `downloads` key `downloads_<id>` (Compose `ProfileScopedKey` parity),
  replace-by-logical-key enqueue, `buildFileName` verbatim
  (`<title>[ SxxEyy[ ep]]_<base36now>.<ext>`, 92-char sanitized stem),
  supported-url gate (magnet/m3u8/mpd/torrent refused), QNAM transfers
  with Range-resume `.part` files under `<config>/nuvio-linux/downloads/`,
  3-attempt retry, Downloading→Paused normalization on load (never
  auto-runs network at boot), in-flight abort on `setProfileId`,
  `pauseActiveDownloads`, `openDownloadsDirectory` (xdg-open — the
  fork's desktop fallback; keeps the lib QtCore-only), playable lookup
  with videoId-first/S-E/movie-fallback order + dir/fileName fallback
  (resolveLocalFileUri parity). Process-wide atomic id ordinal (Compose
  `nextDownloadOrdinal` parity — per-manager ordinals collided within
  the same millisecond, bisect-proven by duplicate-id rows).
- **DL2 playback + UI.** `PlaybackSession::setLocalFileProvider`
  offline-first tier (hit wins over reuse/resolve/torrent, never a
  relay, never cached) wired in main.cpp to the manager; `downloads`
  context prop + 14th profile fan-out target. TransportBar Download
  button (same `!currentIsLocalRelay` gate as External) enqueues the
  live session source with shell-split identity + page toast outcomes.
  `DownloadsPage` route `downloads` (Active/Movies/Shows sections,
  show drill-down with specials-first season groups + series-episode
  sort, pause/resume/retry/play/delete, determinate/indeterminate
  progress, open-folder with failure toast, empty states); completed
  rows play through `requestPlay` so resume + watch state + scrobble
  ride free. Nav: Settings root entry (fork parity) + LibraryPage
  header button (Cloud precedent).
- Gotchas: `pkill -f <name>` matches the invoking shell's own cmdline
  and kills it mid-script (use `pkill -x`); QDesktopServices is QtGui
  in Qt6 (xdg-open instead); `rg -rn` parses as `-r n` replace mode.
- Divergences with reasons: no per-stream download picker (this line
  has no manual stream UI — the playing source is the addressable
  one); no source/proxy header forwarding on transfers (direct urls
  only, same class as the External button); immediate delete, no
  confirm modal (CollectionsPage precedent); no auto-navigate on
  completion (would yank mid-playback); poster/logo/background/
  thumbnail/header fields not carried (rows are text-only like the
  fork's DownloadRow); no completion auto-play hook beyond the
  prefer-local tier (covers next-episode autoplay + replays).
- TEST-LATER: real-http download round-trip on a metered link, folder
  open on a full desktop session, cross-line store read (Compose
  written `downloads_1` rows resolve + play here).

### A4 Episode-release notifications ✅ DONE 2026-09-03 (49/49 ctest, boot clean zero QML errors, smoke PASS vaapi-copy)

- **N1 engine.** `systems/notifications`: verbatim date kernel
  (plain-ISO passthrough, zoned→local, zone-less date part, embedded
  fallback, bare ±hhmm normalization), JVM-`String.hashCode` ids
  (python-verified vectors), `series` normalization, tracked keys,
  `S1E2`/`E5` body shapes, `nuvio://meta` links, followedOn inference
  (savedAt >= 1999-12-31 else today), followedOn-gated request builder
  over `metaFromJson` bodies, sorted store
  (`episode_release_notifications`, key `..._<id>` + Qt-local `_fired`
  companion), manager (reconcile on library change, width-4 Cinemeta
  fetch chain with token stale-guard, single-flight refresh,
  in-flight abort on `setProfileId`).
- **N2 platform (honest divergence).** The fork's desktop backend is a
  stub (authorization denied, schedule/show no-op, page hidden behind
  `AppFeaturePolicy`). Linux has a real bus, so this line ships
  notify-send instead: enabling probes for the binary, refresh fires
  due releases (date <= today) once each, future ones count as
  scheduled and fire on later refreshes; the settings leaf is
  therefore visible here. No daemon exists, so firing only happens
  while the app runs (documented, not fork-silent).
- **N3 sync + UI.** `notifications_settings` is OWNED now (was
  passthrough): orchestrator apply/export legs + T9 suite leg;
  `notifications` context prop + 15th fan-out target + startup
  `refreshAsync` (fork LaunchedEffect parity);
  `SettingsNotificationsPage` leaf (switch, scheduled count,
  test button, status/error/permission lines) + root entry.
- Supporting change: `normalizeVideo` rows additively carry `id` +
  `released` (meta tests still green).
- TEST-LATER: due-fire against a real saved show with a release today,
  test-button delivery on a full desktop session, cross-line
  `episodeReleaseAlertsEnabled` round-trip Compose↔Qt.

### A5 TMDB service + settings ✅ DONE 2026-09-03 (50/50 ctest, boot clean zero QML errors, smoke PASS vaapi-copy)

- **T1 settings.** `systems/tmdb`: 15 verbatim `tmdb_*` keys,
  profile-scoped (`tmdb_settings` file, `..._<id>` keys), enabled&&key
  gate (enable-without-key no-ops, blank-key disables), `_`→`-`
  language normalization, present-only sync envelopes + per-key merge
  apply (Qt debrid convention).
- **T2 id service.** `TmdbService`: media-type normalization,
  prefix-strip id chain, `buildTmdbUrl` (blank values dropped),
  `/find` first-positive pick per type rule, `external_ids` parse,
  per-(id,type) caches both directions, `NUVIO_TMDB_BASE` test seam;
  live suite legs run through a local stub (no network).
- **T3 sync + creds.** `tmdb_settings` is OWNED now (was passthrough):
  orchestrator apply/export legs with the api key stripped at
  assembly (credential policy) + T10 suite leg; the key travels the
  provider-credentials family (provider `tmdb`, field `api_key`) with
  seed/push/merge legs + T1/T2 creds coverage; `tmdb` context prop +
  16th fan-out target; settings changes schedule blob pushes, key
  changes schedule creds syncs.
- **T4 UI.** `SettingsTmdbPage` leaf (enrichment switch gated on key,
  password key row, language row, 12 module toggles gated on
  enabled&&key, fork strings verbatim) + root entry + route.
- Deliberately deferred (P6 list stands): the 2255-line metadata
  enrichment engine (person/entity/more-like-this/collections/
  trailers/episodes/artwork overlays) — the module switches persist +
  sync as honest inert flags until it lands.
- TEST-LATER: live `/find` + `external_ids` round-trip with a real key,
  cross-line `tmdb_settings` round-trip Compose↔Qt, key arrival via
  credential sync from a Compose-seeded server row.

### A6 MDBList ✅ DONE 2026-09-03 (51/51 ctest, boot clean zero QML errors, smoke PASS vaapi)

- **M1 settings.** `systems/mdblist`: 10 verbatim `mdblist_*` keys,
  profile-scoped (`mdblist_settings` file, `..._<id>` keys),
  enabled&&key gate (enable-without-key no-ops, blank-key disables),
  per-id provider toggles (unknown ids ignored), present-only sync
  envelopes + per-key merge apply (Qt debrid convention).
- **M2 ratings service.** Provider table + fetch priority order
  verbatim, `tt\d+` head extraction, movie/show mapping, rating POST
  shape (`{"ids":[imdb],"provider":"imdb"}`), first-rating parse,
  per-(media,id,key,providers) cache, parallel fan-out reordered to
  priority, `NUVIO_MDBLIST_BASE` seam; live suite legs run through a
  local stub (no network).
- **M3 detail wiring (closes a P6 deferral).** MetaPage fetches per
  meta identity when shown (hover-safe: visible-gated like autoplay)
  and renders source-colored text chips in fork display order with
  verbatim value formats (one-decimal/whole/percent); the MDBList
  IMDb chip suppresses the Cinemeta ★ value (hasMdbImdbRating
  parity). The fork's logo drawables are not ported (text chips are
  the honest adaptation).
- **M4 sync + creds + UI.** `mdblist_settings` OWNED (was
  passthrough): orchestrator apply/export with the key stripped at
  assembly + T11 suite leg; the key rides credentials (provider
  `mdblist`, 4-row seed/push/merge coverage); `mdblist` +
  `mdblistService` context props + 17th fan-out target;
  `SettingsMdbListPage` leaf (fork strings verbatim) + root entry.
- Gotcha (session-proven): when pure parse tests pass but live rows
  come back empty, count the STUB's bytes first — this one shipped a
  27-byte body with a 28-byte Content-Length (missing final `}`,
  jsonerr=UnterminatedObject); print QJsonParseError, not just sizes.
- TEST-LATER: live provider fan-out with a real key, chip rendering
  on a real detail page, cross-line `mdblist_settings` round-trip
  Compose↔Qt.

### A7 Membership/supporter ✅ DONE 2026-09-03 (52/52 ctest, boot clean zero QML errors, smoke PASS vaapi-copy)

- **S1 access engine.** `systems/membership`: tiers/entitlements
  verbatim, userId-keyed cached payload (`member_access` file,
  garbage/unknown-tier decode to None), `get_my_member_access` RPC
  with 1/2/4s retry (cached access retained on failure), 15-minute
  re-verification timer, signed-out reads None (this line has no
  anonymous tier), `hasEntitlement()` QML hook for future cosmetic
  gating.
- **S2 overview.** `get_my_membership_overview` mapping verbatim
  (grant/lifetime guards folded at parse, empty array rests
  inactive), same-user refresh keeps the previous overview
  (loading/refreshing split), failures keep state + message.
- **S3 community.** Contributors/supporters-wall DTOs with verbatim
  normalize/sort/key rules, env-overridable URLs (wall defaults to
  nuvio.tv, contributions blank like the build default), ko-fi table,
  level/date formatting; donation progress skipped (desktop policy
  disables it).
- **S4 UI + wiring.** Shared `MembershipCard` (loading/error/
  subscription/connected/grant/non-member states, fork strings,
  Patreon-manage vs donate actions) in the Account section and the
  `CommunityPage` route (tabs, avatars, retry, external links) +
  settings root entry; `memberAccess`/`membership`/`community`
  context props, startup refresh, sign-out clears cached access.
  UserId-scoped like the fork: deliberately NO profile fan-out
  target (18th-slot thinking does not apply here).
- Deferred with reasons: profile backgrounds/avatars fetch + theme
  gating (need the bucket/avatar-catalog features; P7 avatar item
  stands).
- TEST-LATER: live RPC round-trip with a real supporter account
  (tier + entitlements land, card states render), wall fetch against
  the live endpoint, cross-account cache isolation on one machine.

### A8 Plugins runtime ✅ DONE 2026-09-04 (58/58 ctest, boot clean zero QML errors, smoke PASS vaapi-copy)

- **Engine.** `third_party/quickjs` (Bellard 2024-01-13, MIT, same
  lineage as the fork's libquickjs; pinned — quickjs-ng is a
  different fork) + `JsEngine` RAII wrapper (eval, typed native
  globals, JSON, promise-job pumping, deadline interrupt). Needed
  `LANGUAGES C CXX` at root + `-DCONFIG_VERSION` (upstream Makefile
  parity).
- **Polyfill (verbatim data).** `resources/polyfill.js` extracted from
  the fork's JsBindings (fetch/Abort/base64/URL/CryptoJS+subtle/
  TextEncoder/cheerio/require/Array/Object/String) with only the two
  JSON injection slots changed; loader substitutes them at setup.
- **Crypto bridge.** Digests/HMAC via Qt, PBKDF2 verbatim port,
  AES-128/192/256 CBC/ECB via triple-built tiny-AES-c (public
  domain; one-size-per-build forces aes128/192/256.c symbol views),
  hand-rolled GCM-128 (byte-identical to Node on the all-zero
  vectors), RSA/ECDSA-SHA256 via optional OpenSSL (honest errors
  without it). Pinned by RFC/NIST vectors throughout.
- **DOM bridge.** Vendored Gumbo (Apache 2.0) + hand selector engine
  (tag/class/id/attr/descendant/child/comma/:contains) with jsoup
  text/attr/html semantics incl. the empty-attr-undefined quirk.
- **Fetch/URL/console + runtime.** Async promise fetch (UA default,
  redirect toggle, 8KB header truncation, ok:false envelopes),
  QUrl parsing, console capture; `PluginRuntime` executes scrapers on
  worker threads (60s budget) with verbatim result parsing.
- **Repository + streams tier + UI.** Manifest rules verbatim,
  platform tags `{desktop,qt,<os>}` (the fork's `jvm` tag is not
  claimed — honest skip), per-profile state + code cache + global
  scraper settings, server pull (table SELECT) / push (RPC) with the
  empty-server seeds-local rule, width-4 parallel execution fan-out,
  session plugin tier (after addon-direct, before torrent, pending-key
  guarded, never cached), `SettingsPluginsPage` (repos, providers,
  test runs, settings dialogs) + root entry, `plugins` prop + 18th
  fan-out target.
- Gotchas (all session-proven, all first-caught by the suites):
  remember hex ONLY from authoritative sources (my memorized AES/SHA
  vectors were wrong multiple times — OpenSSL/Node over memory,
  always); Gumbo string pieces point into the source buffer (keep it
  alive per doc); element ids mint fresh per call (compare by
  content); `const auto` mutable lambdas can't call their own
  operator(); QML needs Q_INVOKABLE wrappers (std::function never
  crosses); no-op-looking edits eat newlines (anchor multi-line).
- Divergences with reasons: WASM stays a placeholder like upstream
  (the fork's WasmBridge registers nothing); execution waves capped
  at 4 threads (fork uncapped — merged rows identical, latency only);
  no per-scraper group display in StreamsPanel (group flag persists
  for a later panel pass); donation progress absent per desktop
  policy; rating logos render as text chips (no drawable assets).
- TEST-LATER: real repository install + scraper run against a live
  manifest, plugin-URL playback end-to-end, cross-line plugin state
  read (Compose-written rows resolve here).

## Appendix B — Skip / incompatible (normative)

- Direct `.kt`/Compose import: impossible by toolchain (re-express only).
- `androidMain`/`iosMain`/Windows/macOS actuals, `android_*`/`ios_*` keys:
  skip by Linux-target scope.
- JVM packaging + Skiko/AWT workarounds: deliberately dead (replaced by
  distro-Qt + `QQuickFramebufferObject`).
- Contract divergences by design: `vaapi`/`nvdec` pins → `auto`;
  `theme_dark` Qt-local (no Compose bool); unowned blob keys omitted, never
  `{}`/`""`; profile ids server-validated 1–6.

### A9 Home-catalog settings sync ✅ DONE 2026-09-04 (59/59 ctest, boot clean zero QML errors, smoke PASS vaapi-copy)

- **Transport.** New `HomeCatalogSyncController` (standalone RPC pair
  `sync_pull_home_catalog_settings` / `sync_push_home_catalog_settings`,
  platform `home_catalog_shared` — deliberately NOT a settings-blob
  feature, matching the fork's separate `HomeCatalogSettingsSyncService`).
  500 ms debounce, push gated on the initial pull completing for the
  current (userId, profileId) token (profile switch re-gates
  automatically), remote-apply guard, origin client id, merged push
  (cached remote + local-wins).
- **Codec.** New `systems/library` `HomeCatalogSync` pure layer:
  `SyncCatalogItem` / `SyncHomeCatalogPayload` (snake_case,
  encodeDefaults), `mergeSyncJson`, `preferenceKeyFor`,
  `requiresExplicitSyncKey` (>2 colons, non-collection),
  `addonIdForSyncKey` (suffix-strip so colon-bearing manifest ids
  survive), `decomposeLegacyKey` split fallback, missing-flag
  local-default preservation.
- **Store surface.** `HomeShelves::exportSyncPayload` (live definitions
  supply addonId/type/catalogId, legacy split covers the rest;
  collection_/explicit foreign keys round-trip) +
  `applySyncedPayload` (flags always; empty item list keeps local order;
  non-empty replaces with local `heroSourceEnabled` preserved per key
  and live-definition/explicit keys surviving omission). Hero flags
  never cross the wire (local-only both directions, fork parity).
- **Wiring.** main.cpp constructs the controller (token + userId
  providers), `prefsChanged → onLocalCatalogChanged` (covers every
  fork triggerPush call site), auth-gated initial pull, profile
  retarget. No UI changes (shelves + Homescreen page already existed).
- Divergence noted: the fork's normalize caps hero sources at 2 on
  load/apply paths; our reconcile never enforces the cap (pre-existing
  P4 gap, unchanged by this phase).
- TEST-LATER: pull/push round-trip against the live endpoint (needs a
  signed-in account), cross-device shelf-order convergence.

### A10 In-app updater ✅ DONE 2026-09-04 (60/60 ctest, boot clean zero QML errors, smoke PASS vaapi-copy)

- **Kernel.** `systems/updater`: `UpdateVersion` (normalize v/V, token
  split, leading-digit parts, zero-padded compare, unparseable string
  fallback), `UpdateAssets` (Linux selector .deb/.AppImage + arch
  fragments + universal/all fallback, preferred-then-fallback-then-first
  pick), `AppUpdate` release parse (channel match, draft skip,
  prerelease gate, tag←name fallback, malformed vs silent-no-channel
  distinguished).
- **Controller.** `AppUpdater` QObject: silent auto-check once at
  startup, forced/manual checks, ignored-tag (shared Compose desktop
  `nuvio_updater` store), streamed download with progress + .part +
  Content-Length guard + updates-dir clear, xdg-open install + 500 ms
  exit (desktop Linux parity), banner/error/notice states for QML.
- **Product identity (deliberate).** Release feed points at
  JJDizz1L/nuvio-linux-qt, NOT the fork's NuvioMedia/NuvioDesktop (which
  would offer the Compose build to Qt users). With no Qt releases
  published yet, checks honestly report "latest"; once the packaging
  cutover ships .deb/.AppImage assets, updates light up unchanged.
- **UI.** MainShell top overlay banner (tag • size, state subtitle,
  accent progress fill, Notes/Download-Install-Retry/Later — action +
  dismiss hidden while downloading, fork parity) + release-notes
  overlay + shell-toast notices; Settings footer "Check for updates" +
  build version. Overlay, not content-pushing (all 25 routes are
  fill-anchored; noted divergence). `ignoreThisVersion` stays
  controller-only — it has no UI call site in the fork either.
- Gotchas (session-proven): `class QFile*` member forward-declares
  into our namespace (include <QFile>); `QProcess::startDetached`
  children inherit stdio — null them or installer descendants hold
  harness pipes open forever (manifested as a ctest timeout on a
  PASSING suite); the download test's auto-install really launches —
  seam the installer to /bin/true (this box opens .debs in nvim and
  five orphans had to be SIGKILLed).
- TEST-LATER: live feed check once Qt releases exist, real .deb
  download→install handoff on a packaged build.

### A11 Deeplinks ✅ DONE 2026-09-04 (61/61 ctest, boot clean zero QML errors, smoke PASS vaapi-copy)

- **Kernel.** `systems/deeplink`: `parseDeepLink` verbatim port
  (Meta/AddonInstall/Downloads, `auth` reserved-null, stremio
  addon-host-only, provider + path + query-param meta forms, media-type
  aliases, imdb:/tmdb: normalization, addon-host heuristics) +
  `buildMetaUrl`/`buildDownloadsUrl`/`isAppUrl`. All eight upstream
  parser cases pinned plus builder round-trips and edge forms.
- **Router.** `DeepLinkRouter::handleUrl` (trim, blank/scheme guard,
  one signal per link) wired in main.cpp: Meta → `meta.load` +
  push "meta" (title resolves async, every other entry's pattern),
  AddonInstall → push "addons" + registry add (result rides the page
  status + shell-toast "Checking addon…"), Downloads → push
  "downloads". CLI argv filtered case-insensitively (launch-args
  parity); boot with link args verified error-free.
- **Out of scope (packaging cutover).** OS scheme registration
  (.desktop `MimeType=x-scheme-handler`), live single-instance
  forwarding (second launch is a second process until then), macOS
  open-URI handler (no Qt/Linux equivalent). Tracking auth callbacks
  are a no-op set here (device-code/PIN flows, no browser leg).
- TEST-LATER: end-to-end link tap from a browser once scheme
  registration lands, addon-install link against a live manifest.

### A12 Sentry ✅ DONE 2026-09-04 (62/62 ctest, boot clean zero QML errors, smoke PASS vaapi-copy)
- **Kernel.** `systems/diagnostics`: `SentryMetadata` (platform/arch
  buckets incl. the pinned Darwin→windows order quirk, deterministic
  version code, release/dist shapes), `SentrySanitizer` (ignored texts
  + drop predicate), `SentryEnvelope` (DSN parse, envelope URL/auth,
  event JSON with tags/release/dist/env + capped breadcrumbs, never
  request/user/serverName).
- **Settings + client.** `SentrySettings` (shared
  `nuvio_sentry_settings` store, default-on, `supported` = DSN
  configured) + `SentryClient` (start-once, enable-driven active flag,
  lifecycle crumb, 50-capped trail, QNAM envelope POST, pending-crash
  flush). DSN rides `NUVIO_SENTRY_DSN` (empty = unsupported, toggle
  inert); env via `NUVIO_SENTRY_ENVIRONMENT` else build type.
- **Crash hooks (proven, not just wired).** terminate handler +
  SIGSEGV/SIGABRT/SIGILL/SIGFPE/SIGBUS handlers write async-safe
  markers under the shared cache dir, re-raise for core dumps, convert
  to fatal events next launch. A /tmp scratch crasher PROVED
  SIGSEGV→`signal=11` on disk (caught a real bug en route: the pending
  dir was cached WITH its basename while the handler appended it
  again — `pending/pending`, ENOENT, silent loss).
- **UI.** Settings footer crash-reports Switch (bound to the shared
  opt-in; honest "not configured" state without a DSN).
- Divergences with reasons: no sentry-native (no minidumps/symbols —
  that needs split debug info + upload, i.e. the packaging cutover);
  no global Qt message handler (would reroute the journald logging
  pipeline); no offline retry queue (fire-and-forget POST).
- TEST-LATER: live-DSN round-trip (event + previous-session crash
  arriving in the dashboard), sentry-native upgrade with symbols.

### A13 Packaging cutover ✅ DONE 2026-09-04 (62/62 ctest, verify-packaging green, real DEB+RPM+AppImage built, AppImage smoke PASS vaapi-copy)

- **Install + CPack.** `NUVIO_INSTALL` option (default OFF, dev builds
  untouched), FHS install (binary, TorrServer, .desktop, metainfo,
  icon), `cmake/Packaging.cmake` (DEB shlibdeps + libnotify Recommends,
  RPM autoreq; version from PROJECT_VERSION). Real
  `nuvio-linux-qt-0.1.20.0` DEB+RPM built; contents + fields verified
  (`Depends` resolves on Debian builders — this Arch box has no
  shlibdeps db, so its DEB is structural proof; linkage verified via
  `readelf -d` against the documented Qt6+libmpv closure instead of
  hand-pinning per-release package-name lies).
- **Desktop integration.** Entry (Exec `%U`, scheme handlers — closes
  the A11 registration gap), metainfo (validate-clean), hand-authored
  placeholder SVG icon (final artwork is a maintainer call).
- **AppImage.** Auditable ldd vendoring (260 libs + Qt
  platform/xcb/wayland/svg/TLS plugins + QtQuick/QtQml), host-provided
  libc/NSS/GL-driver stack, zsync update-information wired to the A10
  updater. linuxdeploy deliberately NOT used (its bundled strip
  predates DT_RELR — proven fatal on this distro's libraries). The
  41 MB bundle boots clean AND passes the smoke gate itself.
- **Flatpak.** KDE 6.8 manifest, pinned libmpv v0.41.0, finish-args for
  the app's real needs (+Notifications permission: our notify-send
  alerts have no fork equivalent). `--show-manifest` parses.
- **AUR + signing + release.** PKGBUILD (pkgver tracks project, pinned
  by verify script), opt-in detached `.asc` signing (keyless CI stays
  green), tag-driven release workflow publishing the artifacts the
  updater polls.
- **Portals.** No helper code needed (documented): scoped filesystems,
  host-resolved xdg-open, session-bus notifications.
- Gotchas (session-proven): `find -type f` misses symlinked libs —
  the bundler was fine, the counter lied (260, not 15); native
  appimagetool rejects `--appimage-extract-and-run` (AppImage-distro
  flag — env-gate it); appimagetool drops the .zsync in CWD, not next
  to the artifact (deleted one stray from the repo root); `set -e`
  dies silently inside verify scripts when a `$(grep)` finds nothing
  (keep pipelines exit-clean).
- TEST-LATER: first tagged release run (workflow unrun), AUR submit,
  Flatpak full build (needs SDK download), final icon artwork.

## Appendix A — COMPLETE (A1–A13, all committed). Remaining work is user-driven: tagged release, live round-trips in TEST-LATER items, visual re-diffs.

# Nuvio Linux — Qt/QML rewrite feasibility notes

| | |
|---|---|
| **Status** | PLAN ONLY — exploration notes. No product code changed. |
| **Date** | 2026-08-26 |
| **Branch** | `explore/qtqml` (cut from `dev` @ `1ba55e9d`, identical tree) |
| **This file** | `nuvio-linux-qt.md`, gitignored (joins `harbor-pipeline.md`, `cove-pipeline.md`, … as a local research doc) |
| **Decision horizon** | If pursued, this is a multi-quarter commitment. Kill criteria defined in §8 before any work starts. |

> **Addendum (2026-08-26, same day):** Maintainer signaled intent to wind down Compose-line *development* regardless of this doc's original hedge. Consequences adopted into the plan:
> - §5/A0 (skiko direct completion) demoted from "recommended default" to **hedge only — skipped entirely if Qt proceeds**.
> - Next concrete step is **§8/P0 immediately** (throwaway spike, 2–3 weeks) — *before* any EOL announcement.
> - If P0 passes: Compose line moves to **bugfix-maintenance only** while P1+ runs; it remains the parity baseline for the §8 gate and the escape hatch if a P0-scope blocker resurfaces later. If P0 fails: incumbent simply remains the product, ~3 weeks lost.
> - **Maintainer directive (media-timing ownership):** *"let mpv/libmpv control the media like it was made to do."* Adopted as a binding design principle for the rewrite: no application-side frame schedulers, no pull timers, no readback-strategy state machines, no display-FPS overrides. mpv's A/V-sync engine (default `video-sync=audio`) picks the frames; the window system (Qt scene-graph swap → compositor) owns presentation. Evidence in-repo supports the directive: all clean cross-vendor runs were stock audio-sync; every visible regression traces to app-side timing intervention (phantom-grid display-resample lock, report-swap collapse, readback-induced windows scatter).
> - **Maintainer directive (keyboard ownership):** during media playback, **mpv owns the keyboard**: its native default bindings plus every user-defined binding from their mpv config. Concretely: `input-default-bindings=yes`, and when a user `input.conf` exists (discovered in the same chain the bridge already walks for `mpv.conf`: `$MPV_HOME` → `$XDG_CONFIG_HOME/mpv` → `~/.config/mpv` → `~/.mpv`), point the `input-conf` option at it explicitly — libmpv *never* auto-discovers `input.conf` (recorded limitation in this repo's history), deliberate pointing is required. Key events forward from the QML surface via mpv's `keypress`/`keydown`/`keyup` commands. **If no user mpv config is found, binds stay mpv's built-in defaults** — the app never synthesizes a competing media-key map.

---

## 0. TL;DR

A Qt Quick/QML rewrite of Nuvio Linux would exchange:

- **Gains:** native Wayland (killing the entire XWayland/AWT bug family), a true zero-copy GPU video path (deleting the readback pipeline this repo spent ~2 months building and tuning), freedom from the bundled-JRE packaging sagas, lower footprint/startup, first-class desktop integration (QtDBus, tray, MPRIS), and full language homogeneity with the mpv bridge (already C++).
- **Losses:** severing the upstream NuvioDesktop/NuvioMobile KMP relationship — ~123k lines of `commonMain` code whose *wire compatibility* (Supabase sync formats, server-facing identity strings) and whose *future features* come from upstream, and which today can absorb upstream releases nearly wholesale. Plus a realistic **6–12+ month full-time solo** rewrite of ~113k lines of feature/UI Kotlin into C++/QML, re-selecting every library (networking, serialization, images, logging), and re-discovering hard-won platform fixes outside their original context.

**Honest verdict:** Qt/QML is the strongest platform available for a media-centric Linux desktop app, and several concrete pains documented in this repo have no *final* cure inside Compose/Skiko. But the video-path pain — historically the loudest motivation — now has a cheaper, already-proven remedy: the **skiko GL-texture interop direct mode** (spiked successfully 2026-08-24, env-gated, pending direct-mode integration). The Qt move is justified only by *strategic* goals (Wayland-first, footprint, upstream independence), not by fixing playback. Recommendation: **do not start the rewrite now**; land the skiko direct path first, keep this document as the pre-validated playbook if/when strategic priorities shift. If it ever starts, Phase 0 of §8 is written to fail fast and cheap.

---

## 1. Why this document exists

The idea surfaced while weighing why the remaining micro-hitching at the presentation layer has been architecturally unfixable inside our Compose/Skiko stack (see `harbor-pipeline.md` §10): every frame travels mpv GPU → FBO → `glReadPixels` (sync or PBO ring) → CPU copy → Skia bitmap → Skia scene. Harbor-class players hand the GPU texture to the scene graph directly. Two platforms offer that shape on Linux:

1. **Finish skiko interop inside Compose** (option A in `harbor-pipeline.md`) — in-repo, proven feasible.
2. **Move the whole UI to Qt Quick**, whose scene graph is literally designed for exactly this (`QQuickFramebufferObject` + libmpv's official Qt pattern), and which additionally solves problems Compose Desktop never will (native Wayland, no JVM runtime, GC-free hot paths).

This document explores option 2 completely — wins, losses, costs, what survives, what dies, how it would be executed, and when it is and isn't worth doing — so that "should we?" can become an evidence-based decision instead of a vibe.

### Ground rules observed here

- New branch `explore/qtqml` created from `dev`; zero commits on top beyond housekeeping of this document's gitignore entry.
- No source changes, no builds, no releases (per repo hard rule).
- Nothing pushed, ever, without explicit instruction.
- All quantitative claims below are measured from this checkout (see Appendix B) or cite incident history preserved in `AGENTS.md`.

---

## 2. What we would be rewriting FROM (measured)

Raw Kotlin surface of `composeApp` at `dev@1ba55e9d`:

| Source set | Files | Lines | Notes |
|---|---|---|---|
| `commonMain/kotlin/.../app/features/**` | 400 | **108,454** | The product: screens, view models, player UI, integrations |
| `commonMain/kotlin/.../app/core/**` | 74 | 9,465 | Auth/sync/network/tracking/support glue |
| `commonMain/App.kt` | 1 | 4,566 | Navigation host, app shell — one monolith |
| `desktopMain/**` | 129 | 13,444 | Platform actuals: mpv controller + surface/pump, windowing, power, P2P engine |
| **Total Kotlin** | **~610** | **≈136,200** | |

Stack today: Kotlin 2.3.0, Compose Multiplatform 1.11.1, Ktor 3.4.1, Coil 3.5.0-beta01 (SVG included), Kermit 2.0.5, custom file-based stores (`DesktopStorage`; no SQL layer in this checkout).

**How to read these numbers for a rewrite:** essentially *no* Kotlin source survives a Qt move as source. What survives is (a) concepts, protocols and policy re-expressed in C++ (mpv option plumbing, vendor gating, trailer source-selection rules, cache-limit math — much of it documented down to test level), (b) toolkit-independent binaries/artifacts (TorrServer GOAMD64=v1 build, `portal-inhibit-helper`, GPG signing, release tooling skeletons), and (c) the bridge's *mpv-side semantics* — roughly everything in `player_bridge.cpp` below its GL-context/readback layers, which ports nearly verbatim.

A useful decomposition when estimating:

- **UI shell & screens** (~70–80% of `features/`): Compose screens/rails/detail/player-controls/settings → QML pages/components. Declarative QML gives decent throughput per feature after ramp-up.
- **Headless logic inside `features/`** (~20–30%): repositories, extractors, resolvers, sync/outbox flows → straight C++ ports on new dependencies.
- **All cross-cutting libraries**: Ktor→QNetworkAccessManager, kotlinx-serialization→QJsonDocument/nlohmann DTOs, Kermit→QLoggingCategory, Coil→custom async image provider + memory/disk caches, coroutines→QCoro/`std::stop_token` patterns. Each replacement must be chosen consciously (§7), not defaulted into.

Also relevant: the current app is not suffering an architecture gap — the frame pump, caching and playback policies are in their best shape ever post-issue-#13 fixes. We would be rewriting from strength: anything worse than today is a user-visible regression.

---

## 3. Wins

### W1 — Native Wayland support (kills an entire documented bug family)

Skiko has no Wayland backend; our app is structurally X11/XWayland forever (verified against Compose Desktop 1.11.1). Consequences already paid for, from this repo's history alone:

- Tiling-WM corner-box bug on non-reparenting WMs (issue #7), fixed only by the `_JAVA_AWT_WM_NONREPARENTING` env hack — including the discovery that JDK 21 reads it via native `getenv(3)` before any Java API could matter, hence a JNI `setenv` shim as the *first statement of main()*. This class (and all of `AwtNonReparentingSupport`) does not exist under Qt's Wayland platform plugin.
- Fullscreen state-machine fights with AWT/SkiaLayer adapters (issue #8) — replaced by Qt window flags that don't conflate maximized/fullscreen state.
- The Flatpak `--socket=fallback-x11` trap (wayland-only sessions dying with `HeadlessException`) disappears conceptually.
- **The WM_CLASS drift class:** packaged launches derive a different window class than dev runs (`com-nuviolinux-app-MainKt`, 2026-08-24 case), silently knocking us out of Hyprland's `no_vrr` rule — real debugging time spent chasing "judder" that was actually VRR. With Qt, dev and packaged runs share one explicit identity (`setDesktopFileName`, stable Wayland `app_id`); compositors get a single class to rule on.
- Unlocks what Skiko cannot offer at all: true fractional/mixed-DPI scaling without XWayland coordinate games, plus a forward path to Wayland color-management/HDR surfaces as compositors (KWin notably) stabilize them.

### W2 — Zero-copy video path (deleting the readback pipeline)

Today every frame travels: mpv GPU render → FBO → `glReadPixels` (sync by default; PBO ring escalated on weak iGPUs) → CPU copy → Skia bitmap install → skiko scene draw. That pipeline is functional and heavily tuned, but it cost months (issues #13 + follow-ups: slot-pool pump, wake-latch cadence, the sync-vs-ring jitter bisect of 2026-08-22, adaptive escalation, telemetry) and carries inherent costs: ~800 MB/s CPU memcpy at 4K24 peak, an extra frame-period latency in ring mode, and a micro-hitching ceiling acknowledged as architectural (`harbor-pipeline.md` §10).

Qt Quick offers the shape Harbor/cove reach natively:

- **`QQuickFramebufferObject`:** its renderer object lives on the scene-graph render thread owning the GL context — exactly where mpv wants to be. Call `mpv_render_context_create` once there, then per-frame `mpv_render_context_render` targeting Qt's handed-over FBO (`MPV_RENDER_PARAM_OPENGL_FBO`), proc addresses via `QOpenGLContext::getProcAddress`. libmpv ships an official Qt/QML example following precisely this pattern. Subtitles/scaling/tone-mapping stay mpv's job, untouched.
- Net effect: **CPU copies deleted entirely**; pacing becomes mpv-render-thread ↔ scene-graph-swap owned; the whole sync/ring/fence menagerie and its telemetry/escalation heuristics evaporate; and the `display-resample` phantom-grid problem (2026-08-24: vo_libmpv reports no display FPS, feedback self-locking onto consumer draw times) becomes tractable because flip timing approaches scanout reality instead of being measured from `glReadPixels` completion.
- GPU interop upside: dmabuf/VAAPI zero-copy decode still rides `MPV_RENDER_PARAM_DRM_DISPLAY_V2` (unchanged requirement); composition degrades to *texture sampling* of imported surfaces rather than pixel shipping. On NVIDIA/GLX sessions, Qt and mpv share one GLX family in one process by construction — the conflict that forced our headless-pbuffer design simply isn't there anymore.
- Prior art in-repo: same destination as the proven skiko interop spike (`harbor-pipeline.md` option A) — but reached via a platform designed around it rather than reflective bridges across AWT internals (anonymous `SkiaLayer$1`, hierarchy-walk field access, protected-method traps). Both routes kill readback; only this one also delivers W1/W4.

### W3 — Threading and context-ownership sanity

The current stack enforces delicate invariants by discipline: EGL contexts are single-thread (a cross-thread make-current fails `EGL_BAD_ACCESS`), all non-render mpv calls must run on the event thread, blocking JNI (`renderFrame`) may only leave from a producer coroutine, and the readback had to learn that pure event-blocking self-starves. Each invariant produced a real bug class this repo has debugged: GC/console-write hitches on hot threads, the 1 Hz telemetry write stall, the cursor-hijack ownership fix, event-thread shutdown races pinned by `NativeBridgeCoreSelfQuitTest`.

Under Qt these constraints map onto platform primitives instead of discipline:

- One GL context owner by construction — the scene-graph render thread; no second context stack to collide with Skiko's GLX, no "NVIDIA GLX and EGL cannot coexist" landmine requiring provider-selection logic.
- mpv's event-thread rule expressed as a `QObject` living on a dedicated `QThread`; UI talks to it via queued signal/slot connections. Observation caches become properties whose signals marshal to the render thread automatically.
- No JVM on the hot path: no JNI marshalling per frame, no byte-buffer ownership dances, and the entire *class* of JNI drift failures (the 2026-08-25 symbol-drift incident where an unchecked Kotlin extern vs missing C export produced paint-abort black screens inside GL callbacks) is gone — C++ side mismatches are link-time errors.
- Native memory without GC: frame-rate jitter sourced from JIT warmup, allocation churn, or console pipe stalls cannot exist. The Kermit global-gating exercise (`Logger.setMinSeverity(Error)` + double-gated telemetry gotchas) becomes unnecessary — Qt logging is cheap strings when enabled, nothing when not.

### W4 — Packaging liberation (the bundled-JRE sagas end)

Every format currently ships a whole JVM, with its attendant lore: Temurin-only JDKs because Arch/CachyOS builds target x86-64-v3/v4 (the `extra/jdk25-openjdk` AVX-512 regression), the zmm-instruction-count portability checks, `GLIBC_TUNABLES` launcher workarounds, stale-daemon JDK-swap rebuild traps, and the jpackage `pure virtual method called` noise baked into every package at startup.

With Qt as the UI:

- **Arch/AUR:** PKGBUILD gains `depends=(qt6-base qt6-declarative qt6-svg mpv …)` against distro Qt — compiled for baseline x86-64 by construction, no JRE bundling decision ever again. The two AUR packages get strictly simpler.
- **RPM/DEB:** same story — `Requires:`/`Depends:` system Qt6. The Fedora containerized rpmbuild machinery stays only for packaging hygiene (header digest correctness), no longer for runtime construction.
- **Flatpak:** the app could move from GNOME-runtime-plus-bundled-everything toward `org.kde.Platform`, which ships Qt natively (Stremio-parity NVIDIA story we already emulate manually today). Note honestly: our codec-rich libmpv/ffmpeg source modules remain **either way** — they serve decode coverage, not the toolkit, so W4 shrinks but does not vanish under Flatpak.
- **AppImage:** `linuxdeployqt` bundling relocatable Qt .so files is standard, well-trodden practice.
- Footprint: roughly tens of MB of app payload instead of ~90 MB+ of JRE/Compose/skiko; startup skips JVM boot and Compose warmup; idle RAM drops accordingly (no image-cache-by-default-JVM-heaps decisions — Coil's 128 MiB cap was itself a heap-deficit patch).

### W5 — Desktop-integration maturity gains

QtDBus gives first-class access to things we hand-roll through subprocess/dbus dance today and to capabilities we don't have yet:

- **MPRIS2 media player interface** (new capability: desktop media keys, KDE/GNOME shell integration) — not present anywhere in the current build.
- System tray, notification portals, global shortcuts via mature Qt APIs.
- The screensaver-inhibit chain keeps working unchanged conceptually (`systemd-inhibit` → portal v3 helper → PolicyAgent → legacy); the portal helper binary is toolkit-independent. If anything, the direct-dbus fallback tiers simplify.

### W6 — Language homogeneity with the video core

The mpv bridge is already C++ and is where our deepest platform knowledge lives (NVIDIA gating, DRM-node matching tiers, hwdec runtime fallback thresholds, mpv 0.41 ABI shifts). A Qt app collapses UI and video into one language, one toolchain, one debugging story (gdb/valgrind/rr over the whole app instead of JVM↔native boundary archaeology). The bridge's JNI layer — 38 exported symbols package-name-coupled to Kotlin (`Java_com_nuviolinux_app_features_player_desktop_…`), a rebranding hazard that has already bitten once (v0.1.15.2) — simply ceases to exist; its GL-context/readback halves are deleted per W2 while its option/cache/vendor-policy core ports as-is.

### W7 — Upstream-independence *optionality*

This fork deliberately tracks upstream NuvioDesktop (version-aligned at `0.1.20-alpha`, wholesale-ported once already). That relationship is currently net-positive; but it also means product direction, UI vocabulary, and player architecture (notably: upstream moved to a vendored GStreamer `compose-media-player`, diverging from our libmpv anyway) belong partly to someone else. A Qt rewrite is simultaneously a declaration of platform sovereignty: it converts "how do we absorb upstream this quarter" into "we choose what to adopt, on our schedule". Whether that is a win depends entirely on how much the upstream relationship keeps paying — which is precisely the kind of question §6 turns into explicit trigger conditions rather than vibes.

---

## 4. Losses & costs

### L1 — Severing the upstream KMP relationship (the decisive loss)

~123k lines of `commonMain` exist not because we love duplicating code, but because they *are* the shared protocol with upstream NuvioDesktop/NuvioMobile:

- **Wire compatibility is mandatory forever:** Supabase sync parity demands server-facing identity strings stay byte-identical (`clientName = "Nuvio Desktop"`, `User-Agent = NuvioMobile/<ver>`, `client_instance_id` prefix `nuvio-mobile-`, `p_platform="desktop"`) plus faithful re-expression of sync blob formats (`ProfileSettingsSync` payload incl. `@SerialName("discord_settings")`-style schemas), cross-client credential push/pull/delete flows, and the watch-progress-source outbox semantics. A C++ port must reproduce all of it exactly, or users running both apps silently corrupt/clobber each other's settings.
- **Every future upstream feature becomes manual labor:** today an upstream release can be absorbed nearly wholesale (the 0.1.20-alpha port precedent: previews, track logic, extractors, resolvers in days-to-weeks). Post-fork each such wave requires re-porting through two languages instead of cherry-picking through one. This cost compounds for the life of the project.
- **Upstream is also our spec donor:** track-preference behavior, trailer extraction client chains (visionos→android_vr→android→ios), auth flows — pinned by tests copied from upstream patterns. Losing the merge path means losing cheap access to their bug fixes.
- Mitigation if proceeding (§7): freeze-and-pin serialization with contract tests against the production backend, treat upstream as occasional spec donor only, accept divergence openly.

### L2 — Scale and calendar honesty

Rewriting ~136k lines of Kotlin (≈113k of it feature/UI) into C++/QML as a solo maintainer, while the shipped Compose app keeps receiving bug reports and releases:

| Scenario | Assumptions | Calendar |
|---|---|---|
| Aggressive | Full-time 40 h/wk, QML ramp-up ≤2 wks, parity scoped *tightly* to current features, tests ported opportunistically | **~6–9 months** |
| Realistic | Full-time with interruptions; current-feature parity + regression tracking against live users | **9–14 months** |
| Side-project | Evenings/weekends alongside maintaining the existing app | **18–24+ months**, high abandonment risk |

These are engineering estimates with ±50% error bars, not commitments. The structurally dangerous part is not the total — it is the *feature freeze during transition*: every week spent on the rewrite is a week the competitive baseline (Stremio-class apps, upstream Nuvio itself) keeps moving. Half-ported states ship nothing.

### L3 — Full dependency re-selection (nothing comes for free)

Each layer replaced by a Qt/C++ equivalent that must be chosen and learned deliberately:

| Today (Kotlin) | Under Qt | Notes / risk |
|---|---|---|
| Ktor client (CIO), OkHttp in extractors | `QNetworkAccessManager` (+`QNetworkReply` cancellation semantics) | HTTP/2 fine; TLS via OpenSSL — but our Flatpak static-openssl lesson still applies |
| kotlinx.serialization DTOs (dozens: Trakt/Simkl/TMDB/Stremio-addons/Supabase) | Manual `QJsonObject`/nlohmann mapping or codegen | Highest silent-corruption risk area; L1's wire formats live here |
| Coroutines/Flow | `QCoro`, signals, `std::stop_token` | Cancellation semantics differ; deliberate audit per flow |
| Coil3 (+SVG, memory/disk caches, the 128 MiB cap) | Custom `QQuickAsyncImageProvider` + own caches | Small, well-understood component; must re-implement disk cache |
| Kermit logging + gating discipline | `QLoggingCategory` | Strictly easier; console-pipe stall class gone |
| `DesktopStorage` file stores | Same file formats, new reader | Keep formats byte-compatible or accept a one-time migration |
| DataStore-style profile sync blob | C++ re-write of same JSON contract | See L1 |

None of these is individually hard. The loss is the *aggregate attention*: each choice made casually now becomes a constraint inherited later.

### L4 — Bug-archaeology reset (the rediscovery tax)

`AGENTS.md` is effectively a ledger of paid tuition — incidents whose fixes carry context that doesn't transfer automatically. Triage of what survives vs. dies vs. must be re-derived:

| Category | Fate under Qt |
|---|---|
| mpv semantics: option precedence, user-config loading order, cache math (`demuxer-max-back-bytes` = min(setting/4, 64 MiB)), auto-profile escape hatch, ABI shifts | **Ports verbatim** (bridge core) |
| Vendor gating (NVIDIA DRM-node suppression), hybrid-GPU node matching tiers, hwdec runtime fallback counters | **Ports nearly verbatim** |
| Trailer source policy + its selection tests, TorrServer cache/idle-stop logic, Discord RPC protocol client, Trakt/Simkl flows | **Re-implemented** in C++ from Kotlin spec/tests |
| EGL/GLX provider selection, PBO ring, readback telemetry, slot-pool pump | **Dies happily** (obsoleted by W2) |
| AWT non-reparenting shim, fullscreen state machine, window-state storage | **Dies happily** (W1) |
| Flatpak env choreography (LIBVA_DRIVER_NAME gating, nvidia-vaapi-driver paths) | **Must be re-derived** for `org.kde.Platform` layout — the knowledge transfers, every path literal changes |
| Screensaver inhibit chain (portal v3 helper & friends) | **Survives as-is** (toolkit-independent) |

The honest framing: the *hard* platform lessons mostly live mpv-side and survive; the *numerous* UI-side lessons die and are re-learned as new-but-different Qt bugs instead.

### L5 — Maintainability profile shift

- **C++ memory safety enters the whole app**, not just the bridge: use-after-free/UAF hazards around scene-graph object lifetimes (`QQuickFramebufferObject`'s renderer lifecycle is subtle by design), threading bugs that segfault instead of throwing.
- **QML drags JavaScript in:** every `property var`, binding expression and inline function is dynamically-typed runtime code — the bug class moves from compile errors to silent no-ops unless disciplined (strict typed properties, C++ models exposed properly).
- **Licensing duty:** repo is GPL-3; Qt under LGPLv3 is fine *only with dynamic linking* (static linking needs a commercial license). All four shipping formats already ship dynamic libs, so this is procedural — but AppImage/RPM bundling of relocatable Qt .so's carries rpath obligations forever.
- **Bus factor 1:** today there is one maintainer who knows JVM+Kotlin+mpv deeply. A rewrite would trade that for one maintainer needing to know C++/Qt/QML/mpv equally deeply before being productive on any layer.

### L6 — Transition double-maintenance

While both stacks exist: dual release QA (every format × hardware matrix), duplicated triage ("does it repro in Compose or Qt builds?"), AUR/repo naming decisions (`nuvio-linux` staying Compose indefinitely? a separate `-qt` package until cutover?), and CI multiplied. The README-grade story ("which app should I install?") gets worse before it gets better.

### L7 — Unknown unknowns priced into Phase 0 only

Genuinely open questions we cannot answer from armchairs, each a §8 kill criterion rather than an assumption:

1. Does VAAPI dmabuf interop behave identically when surfaces land via Qt's shared scene-graph context across all three test GPUs (AMD RDNA4 known-good elsewhere, RTX 3070 GLX-family, Intel iGPU)?
2. Wayland fractional-scaling under `QQuickFramebufferObject` with mixed-DPI monitors — pixel-perfect or letterboxed?
3. Scene-graph throughput with dozens of poster/rail items at 4K — equivalent to our current skia behavior or better/worse?
4. Qt 6.x minor-version spread in distro wildcards (Debian stable ships older Qt; our DEB Depends floor choice matters).
5. HDR/color-management readiness is compositor-gated even under Qt — not a launch feature regardless.

---

## 5. Alternatives compared

| | **A0 — Stay Compose + finish skiko interop** (recommended default) | **A1 — Qt/QML rewrite** (this doc) | A2 — Hybrid: Qt UI over JVM logic | A3 — GTK4 / Rust / Tauri |
|---|---|---|---|---|
| Kills readback/CPU copies | ✅ (spike proven 2026-08-24) | ✅ natively | ⚠️ (same interop problem, extra hop) | GTK4 offload: ✅ for GStreamer; Tauri/webview media: weak |
| Native Wayland | ❌ never (Skiko) | ✅ | ✅ (UI) but JVM process remains | GTK: ✅ |
| Packaging simplification | ❌ JRE stays | ✅ biggest W4 | ❌ worst of both | varies |
| Upstream absorption stays cheap | ✅ | ❌ ends (L1) | ❌ | ❌ |
| Calendar to user-visible win | **days–weeks** | 6–12+ months | similar-to-worse than A1 | Rust: doubles port cost vs C++ |
| Fits existing expertise | ✅ deep now | partial (C++/mpv side deep; QML new) | split brain | new language(s) |

**A0 details, because it matters for the decision:** the skiko spike already proved on RDNA4/skiko 0.144.6 that skiko's `DirectContext` is reachable and GL-content enters the scene zero-copy (`Surface.makeFromBackendRenderTarget` → exact pixels verified). The direct-mode design is scoped and pending integration. If completed, the *primary historical motivator* for considering Qt (presentation-layer micro-hitching, readback cost) is addressed at ~5% of the calendar cost of A1.

**A2 explicitly rejected:** no maintained Qt↔JVM bridge exists worth betting a product on (QtJambi's maintenance history is not a foundation), and running two runtimes in one process reproduces today's context-conflict problems with worse documentation.

**A3 summary:** GTK4's `GtkGraphicsOffload` + GStreamer paintables is genuinely competitive architecturally, but it demands re-implementing all Kotlin business logic in Rust/Vala/C — strictly more work than A1 with a smaller ecosystem for mpv-adjacent patterns (Harbor precedent notwithstanding). Tauri ships webviews whose media pipeline we'd fight forever.

---

## 6. When Qt IS the right call (explicit triggers)

Do not start this for playback smoothness alone — that's §A0. Start it only if one or more of these becomes true and stays true:

1. **Wayland-first becomes a product goal**: complaints from pure-Wayland/fractional-scaling/HDR-curious users accumulate faster than XWayland mitigations satisfy them.
2. **Upstream coupling turns net-negative**: NuvioDesktop's direction diverges enough (architecturally or legally/licensing) that absorbing their releases costs more than re-implementing chosen features independently.
3. **Footprint/perception pressure persists post-A0**: if even after a direct GPU path, "why does a video app need 500 MB RAM and a JVM" remains a recurring adoption blocker.
4. **Commitment bandwidth exists for ≥12 months**, accepted in writing, with the shipped app maintained in parallel until P5 parity gate (§8).
5. A proof-of-concept shell (P1 scope below) demonstrably reaches visual/interaction parity for the home/detail/player loop without discovering platform blockers — validated against §8's kill criteria on all three GPUs and both session types.

---

## 7. If we go: target architecture sketch

- **Shell:** Qt Quick Controls 2, single `QQmlApplicationEngine`; navigation via a stack based on our current route model; strict typed QML properties, C++-exposed models only, no `.js` files.
- **Media-timing ownership principle (binding):** the app owns *nothing time-shaped*. It configures mpv (options/cache/hwdec policy), relays commands, and draws overlays. Frame selection = mpv's sync engine at its defaults (`video-sync=audio`); presentation = scene-graph swap under the compositor's clock. No app-side cadence logic anywhere; the legacy env knobs (`NUVIO_VIDEO_SYNC`, `NUVIO_REPORT_SWAP`, `NUVIO_READBACK`, `NUVIO_VIDEO_TIMING_OFFSET`) are deleted, not ported. mpv's render-update callback merely marks the QML item dirty; Qt's vsync-driven scene-graph pass is the consumer, which removes the self-starve failure mode structurally (consumption rate ≈ compositor refresh ≥ video rate — the "pull component is load-bearing" pathology belonged to the readback design alone).
- **Keyboard ownership during playback (binding):** `input-default-bindings=yes`; when a user `input.conf` is found next to their `mpv.conf` (same discovery chain), set `input-conf=<path>` before init. While the video surface holds focus, Qt key events are translated (Qt::Key → mpv keysym map, a bounded known-good subset mirroring mpv's `input.c` naming) and enqueued as `keypress`/`keydown`/`keyup` on the bridge's existing command queue (event-thread rule preserved). Resolution order inside mpv: user `input.conf` last-wins over native defaults. Keys no binding claims **fall through** to QML app handling (overlays, page nav) — the app keeps zero media keys of its own. Embedded-mode nuance accepted knowingly: window-concept default binds (`f` fullscreen, window-scaling) are inert under `vo=libmpv`; left enabled because selective suppression of what stock-mpv users expect breeds input conflicts. Current Compose behavior (app-owned arrows/space/F11 handlers) is retired by this directive.
- **Video:** one `MpvQuickItem : QQuickFramebufferObject`. Its renderer (scene-graph thread) creates the mpv render context once against Qt's GL context, renders per-frame into Qt's FBO; property observations surface as `Q_PROPERTY` notify signals marshalled to the UI thread. Bridge core (options/cache/vendor-policy/event-thread) ports from `player_bridge.cpp` minus its entire GL-context/readback halves. Direct-mode env knobs (`NUVIO_VIDEO_PATH` etc.) retire into build-time constants.
- **Concurrency:** event thread = dedicated `QObject` on a `QThread`; producers via `QCoro` where ergonomics matter; no lock-free CAS handoff needed anywhere near frames (Qt owns queueing).
- **Identity/sync module (the L1 containment zone):** one directory owning every upstream-parity constant (identity strings, blob schemas, endpoint paths), gated behind contract tests executed against the real backend before each release; divergence beyond it is free.
- **Toolkit-independent survivors stay untouched:** TorrServer orchestration incl. idle-stop/cache settings, `portal-inhibit-helper`, GPG/release manifest machinery, `dist/desktop` assets, YouTube-extraction policy tables (re-written, same rules, same test vectors).
- **Packaging:** Arch/AUR/RPM/DEB against distro Qt6; Flatpak considers `org.kde.Platform` while keeping codec modules; AppImage via linuxdeployqt; versioning policy may finally decouple from upstream (`VERSION_NAME`) at cutover.

### 7.1 Source & module structure (draft)

**The measured problem being designed away** (at `dev@1ba55e9d`): two twin ~4.6k-line monoliths — `App.kt` (4,566 lines: navigation host + shell + routing + image config) and `player_bridge.cpp` (4,578 lines: GL provider selection, EGL/GLX bootstraps, readback machinery, event thread, caches, options, JNI layer — one scope) — plus **22 further Kotlin files above 1,000 lines** (`PlaybackSettingsPage` 3,420, `CollectionEditorScreen` 2,610, `MetaDetailsScreen` 2,431, …). The bridge monolith is concretely *why* recent maintenance hurt: the 2026-08-25 JNI-symbol bisect needed surgery inside a file whose concerns share a single compilation unit, and the package-rename coupling became a repo-wide hazard precisely because everything lived together. Neither pattern may recur by construction.

Proposed shape — **one directory = one CMake target = one testable unit = one review scope:**

```
<repo>/                          # CMake superproject; add_subdirectory per system
├── CMakeLists.txt               # options per target; nuvio_* static libs -> one exe
├── app/                         # EXECUTABLE SHELL ONLY (~100 lines total)
│   ├── main.cpp                 # QGuiApplication, engine load, explicit wiring
│   └── bootstrap/               # logging categories, XDG paths, single-instance, CLI
├── systems/                     # DOMAINS - headless libraries, no QtQuick includes
│   ├── mpv/                     # THE video system; sole owner of anything libmpv
│   │   ├── MpvQuickItem.*       #   UI-thread item: Q_PROPERTYs, key forwarding
│   │   ├── MpvRenderer.*        #   scene-graph callback: GL ctx/FBO ownership
│   │   ├── MpvController.*      #   private-QThread QObject: handle lifecycle,
│   │   │                        #     options, observation caches, command queue
│   │   ├── MpvUserConfig.*      #   conf discovery chain; explicit input-conf pointing
│   │   ├── HwdecPolicy.*        #   vendor gating, DRM-node tiers, fallback counters
│   │   └── TrackListModel.*
│   ├── playback/                # orchestration ABOVE mpv (mpv types don't leak out)
│   │   ├── PlaybackSession.*    #   source assembly incl. audio-file pairing
│   │   ├── trailer/             #   extraction policy tables + resolver + LRU
│   │   └── p2p/                 #   TorrServer process mgmt, settings protocol, idle-stop
│   ├── library/                 # catalog domain; providers live as siblings
│   │   ├── network/QnamFactory  #   one client factory: timeouts/TLS/cache-control
│   │   ├── tmdb/ trakt/ simkl/ stremio/
│   │   └── catalog/             #   repositories, rails, search on top of providers
│   ├── authsync/                # L1 CONTAINMENT ZONE - upstream wire parity only
│   │   ├── ServerIdentity.h     #   byte-exact constants; editing it runs tests
│   │   ├── supabase/            #   auth flows, sync-blob codec, outbox semantics
│   │   └── contracttests/       #   QtTest wire suite = release gate (P4)
│   ├── tracking/                # scrobble coordinator, provider registration
│   ├── integrations/            # discord/, power/ (inhibit chain), mpris/
│   ├── settings/                # profile-scoped stores (DesktopStorage-format readers)
│   └── platform/                # window-state store, fullscreen policy, display info
├── ui/                          # VISUAL layer - depends downward only
│   ├── viewmodels/              # QObject facades per screen; zero QtQuick includes
│   ├── image/                   # AsyncImageProvider: memory cap + disk cache
│   ├── theme/                   # design tokens -> one QQC2 style
│   └── qml/                     # pages/ components/ - declarative ONLY, no logic
└── native/helpers/              # portal_inhibit_helper.c untouched (own binary)
```

Dependency arrows, enforced at link time: `ui/viewmodels → systems/*`; `playback → mpv/` through mpv's public header only; `systems/* ↛ ui`; no horizontal sibling imports except through declared interfaces; `authsync` imports nothing above itself; `app/main.cpp` constructs everything explicitly — **there is no god object**.

### 7.2 Why this shape makes the most sense (the reasoning, not just the picture)

1. **The monoliths are measured liabilities, not style opinions.** App.kt and player_bridge.cpp each ~4.6k lines meant: every edit risks unrelated-scope breakage, bisects are slow (2026-08-25), review diffs are unreadable, and grep-replace hazards propagate repo-wide (v0.1.15.2 rename). In C++ the cost compounds — compile times and header coupling punish big files harder than Kotlin ever did. Small files with narrow headers aren't aesthetics; they're the compile-speed and correctness strategy.
2. **CMake's target graph makes illegal structure unbuildable.** This is the single biggest structural win over the current tree: today "who may call whom" lives in convention and code review; as static-lib targets with explicit `target_link_libraries`, a systems module including UI headers is a *link error*, not a discussion. Architecture survives maintainer fatigue.
3. **The event-thread rule becomes physical instead of documented.** Only `MpvController` owns mpv handle access and it lives on its own QThread with a signal/slot API — calling it from the render or UI thread without a queued connection is a mistake you can't express easily, replacing today's discipline-only invariant that produced real races before being pinned by a test.
4. **L1 risk is quarantined to three paths.** Server-parity constants, blob codec, and contract tests are one directory; the other 90% of the app can evolve freely knowing it cannot corrupt sync wire compatibility accidentally. Today those strings live sprinkled across core classes.
5. **Policy modules become table-testable in isolation.** HwdecPolicy, trailer source-selection tables, cache-limit math, hwdec-fallback counters — pure functions with no GUI deps → QtTest targets where the existing Kotlin test vectors port nearly mechanically. Under the monolith regime these were testable only through JNI ceremony (and that test harness broke silently for weeks).
6. **Systems have independent lifecycles — birth AND death.** Discord RPC appeared as one self-contained protocol client; under this layout integrations/discord is adopted wholly and deleted wholly (screensaver tiers die if portals make them obsolete; MPRIS appears as its own file). No cross-cutting surgery. It also mirrors packaging reality: distro deps map to system modules one-to-one.
7. **Phase-aligned growth (maps onto §8):** P0 builds only `app/` + `systems/mpv/`; P1 adds `ui/` rails + `library/catalog` + `settings/`; P3 fills `integrations/`, `p2p`, `trailer/`; P4 adds `authsync` last but writes its contract tests first. The structure never exists all-at-once, so there's no big-bang scaffolding risk.

**Discipline rules attached to the structure** (record now, enforce in CI later):

- Soft caps: C++ files ≤ ~400 lines, class responsibilities ≤ ~500; QML files ≤ ~300 lines with zero imperative logic; h/cpp pairs always; public headers minimal (pimpl or forward-declares across boundaries).
- Viewmodels expose State/commands only; QML binds, never computes; no singleton service locator — wiring is visible in `main.cpp`.
- Every system gets a `<system>/README.md` stub (≤30 lines) saying what it owns, what it must never include, and which tests guard it — cheap insurance for bus-factor-1.
- One registered logging category per system (`nuvio.mpv`, `nuvio.authsync`, …) so §"debug capture" stays greppable per subsystem from day one.



---

## 8. Phased execution plan (only if triggered; each phase has kill criteria)

**P0 — Feasibility spike (2–3 weeks, throwaway code, `explore/qtqml`)**
Build a bare QML window + `MpvQuickItem` playing local files and one HLS stream. Validate the matrix: {RDNA4 RX 9070 XT, RTX 3070 driver 610.x, one Intel iGPU} × {X11, Hyprland-Wayland, KDE-Wayland} × {native run} (+Flatpak smoke for org.kde.Platform in parallel). Measure (confirmation only — this spike tunes *nothing*): mpv runs with stock defaults (`video-sync` untouched); verify hwdec-current behavior, CPU% at 4K24 HEVC-Main10 vaapi zero-copy, fractional-scaling correctness, and a lightweight diagnostic counter that published-frame rate tracks mpv's selected frames without any app-side timer object existing in the codebase. Validate keyboard ownership end-to-end while it's nearly free to test: forwarded Space/←/→ act through mpv's default binds, and one scratch `input.conf` custom bind activates via explicit `input-conf` pointing — proving both the no-user-config (defaults only) and with-user-config resolution paths before P2 depends on them.
*Kill criteria:* any GPU/session combination that can't reach flicker-free 4K24 after honest effort; VAAPI dmabuf import regressions vs current app on any tested GPU; Wayland scaling broken; or the temptation arising to add app-side pacing fixes to make it smooth — that outcome means the architecture violates the ownership principle and must be rethought, not papered over.

**P1 — Skeleton product shell (4–6 weeks):** navigation, home rails (image loading + caching), detail page skeleton, settings persistence via file stores. *Kill:* rail scrolling jank at 4K that resists scene-graph hygiene; image-cache ballooning systematically worse than Coil's capped behavior.

**P2 — Player feature parity (6–10 weeks):** controls UI, track preference auto-selection rules ported with tests, volume/seek/pause ownership semantics, fullscreen/window-state, cursor idle-hide, buffered-indicator states.
 
**P3 — Integration parity (6–8 weeks):** TorrServer engine (settings GET-modify-POST protocol!), trailer extraction + hover previews + hero trailer rules, Discord RPC client, MPRIS (new), screensaver chain wiring.

**P4 — Auth/sync/settings sync parity (4–8 weeks, riskiest):** Supabase auth flows, sync blob read/write byte-compatibility, credential push/pull, outbox semantics, **contract tests against production backend as release gate.**

**P5 — Packaging cutover + LTS freeze of Compose line (3–5 weeks):** all formats rebuilt, signed, sha256 manifests; final Compose release tagged LTS and maintained bugfix-only until Qt line proves itself on distro-wildcard systems; AUR strategy decided explicitly (replacement vs parallel package).

**Parity gate definition:** "a user of record R cannot name a workflow available in Compose-build N that is missing or worse in Qt-build M" — audited against this repo's known feature ledger, not vibes.

---

## 9. Open questions to answer before (or during) P0

1. Is there real user demand signal for Wayland-native/fractional-scaling/HDR — issue tracker counts, not anecdotes? (Feeds trigger §6.1.)
2. What is upstream NuvioDesktop's apparent roadmap velocity over the next ~6 months (feeds §6.2 — how much are we giving up)?
3. Does a macOS/Windows port ever enter the picture? (Qt makes it plausible-later; Compose already claims cross-platform — but our Linux-specific bridge wouldn't port.)
4. Solo-maintainer bandwidth reality-check for ≥12 months, accepted in writing, before P1 (not just P0).
5. Would we keep the name/branding and repo, and treat Qt line as NuvioLinux 1.x? Repo rename/package naming implications early, not at P5.

---

## 10. Immediate next steps taken here

- Branch `explore/qtqml` created from `dev@1ba55e9d`. ✔
- This document written; added to `.gitignore` alongside other local research docs. ✔
- Nothing else: no source changes, no builds, no pushes (per instruction and per repo hard rule on builds/releases).

If/when green-lit: start §8/P0 only after skimming §5/A0 once more — the honest default remains finishing skiko direct mode first and letting §6 triggers accumulate evidence.

---

## Appendix A — Evidence index (into repo history / AGENTS.md)

- Readback pipeline construction & tuning: issues #13 (+2026-08-21 PBO follow-up, 2026-08-22 jitter bisect) → W2/L4.
- Skiko interop feasibility proof (magenta-pixel test, 2026-08-24) → §5/A0, W2 relation.
- display-resample phantom-grid analysis (2026-08-24) → W2 pacing argument.
- XWayland/AWT class: non-reparenting shim, HeadlessException trap, fullscreen #8 machine, WM_CLASS/no_vrr drift (2026-08-24) → W1.
- JNI symbol-drift black screens (2026-08-25), rebranding coupling v0.1.15.2 → W3 homogeneity argument.
- Packaging sagas: Temurin-baseline requirement, x86-64-v4 JDK regression, GOAMD64=v1 TorrServer rebuild → W4.
- Server-facing identity strings & sync parity contracts (clientName/User-Agent/client_instance_id/outbox) → L1 verbatim-port requirement.

## Appendix B — Measurement commands used for §2 (re-run anytime)

```bash
find composeApp/src/commonMain -name '*.kt' | wc -l          # 480
find composeApp/src/commonMain -name '*.kt' -exec cat {} + | wc -l   # 122767
for d in composeApp/src/commonMain/kotlin/com/nuviolinux/app/*; do
  echo "$(basename $d): $(find $d -name '*.kt' -exec cat {} + | wc -l)"
done                                                          # features: 108454 …
grep -E '^kotlin|^composeMultiplatform|^coil|^ktor' gradle/libs.versions.toml
```

*End of document.*

### Settings-store format (P1 spec extracted)
DesktopStorage persists per-store as **java.util.Properties** (`Properties.store/load`) under `<configDir>/nuvio-linux/<store>.properties`(+state variants); values: String/Boolean/Int/Float/Set<String> typed accessors; private 0600 perms. ⇒ C++ port = Properties-format reader+writer (backslash escapes \ =:#! , leading-space trim rules, ordered store on save NOT required — Java writes arbitrary order w/ timestamp comment line first to skip). Byte-compatible enough for shared profiles; JSON blobs are single String values.

### P1 kickoff (next session, first commit target)
systems/settings: PropertiesStore.{h,cpp} — faithful java.util.Properties codec (escape rules above in nuvio-linux-qt.md), atomic tmp+rename write, 0600 perms, typed get/put incl Set<String> as comma-list parity check against a fixture captured from real Compose profile. THEN wire NavigationModel skeleton.
### Window identity research (explore/qtqml, VERIFIED live)
Hyprland derives client fields from XDG shell protocol:
- xdg_toplevel.app_id -> hyprctl class (+ snapshot initialClass at map time)
- surface set_title -> title (+ initialTitle snapshot)
Qt maps setDesktopFileName(X) to app_id X directly.
Verified live on our P0 binary:
class=nuvio-linux initialClass=same (RESOLVED 2026-08-26: user chose bare package name `nuvio-linux`, NOT the io.github id initially probed — main.cpp setDesktopFileName("nuvio-linux"))
title=Nuvio Linux xwayland=False size=logical-px floating=False
Implications:
1. Zero class drift under Wayland/Hyprland: app_id is compile-time constant in main.cpp; dev==packaged by construction. Kills Compose-era drift bug family.
2. omarchy video_class lua gate does not match this id yet: VRR/no_dim/idle_inhibit inactive for Qt line until user extends video_class or final id decided.
3. Ownership: user config + release docs; never hardcode in app.
4. Repro: launch binary; hyprctl -j clients | grep io.github.jjdizz1l

## Execution roadmap (2026-08-26 review — phased TODO)

### Phase 1 — Settings foundation (in progress)
- [x] PropertiesStore single-pass clean rewrite — DONE 2026-08-26, all suites green (codec half is proven; store methods were rolled back after escape-layer patch damage). One authoritative write vs existing header; ctest green incl parity fixture.
- [x] Set<String> JSON accessor — kotlinx-style JSON array string; quote/backslash/comma round-trips verified.
- [ ] Commit `feat(qt/settings): PropertiesStore complete`.

### Phase 2 — Input directive acceptance
- [x] Space/Right via mpv defaults - FULLY VERIFIED 2026-08-26: automated leg PASS + USER VISUAL CONFIRM (Space toggle, arrows seek; f inert as designed - vo=libmpv ignores windowing binds, compositor owns fullscreen). Phase 2 COMPLETE.
- [x] Scratch input.conf custom bind loads via explicit input-conf pointing; user binds win over defaults inside mpv. (keytest harness PASS 4/4 — Space-pause/resume, Right default seek, F7 custom bind.)
- [x] Document result here + AGENTS.md one-liner.

### Phase 3 — Navigation skeleton (P1 proper)
- [x] NavigationModel - headless suite green (3/3 total); wired as QML context prop driving home/video/library routes.
- [x] PosterProvider (QQuickAsyncImageProvider + NAM, FIFO cache 64) - 12/12 metahub posters clean; user-verified nav flows.
- [x] Cinemeta catalog federation (CatalogService) - offline parse/mapping suite green (6/6); LIVE VISUAL CHECK DEFERRED to integrated milestone per maintainer.
- [x] SettingsPage reading AppSettings->PropertiesStore - decoderMode/cacheMb persisted+live-pushed; darkTheme reactive through Theme singleton (user toggled light/dark live); decoder combo live-pushes hwdec into running core; persistence user-verified across relaunch (832MB slider round-trip).

### Phase 4 — Re-validation matrix
- [ ] Fractional scaling 1.25 re-check (toggled off mid-P0).
- [ ] NVIDIA leg (RTX 3070 system, nvdec-copy path) — user boots when ready; NUVIO_MPV_HWDEC override, no rebuild.
- [ ] Intel iGPU leg (vaapi native).

### Phase 5 — User-side decisions (not code)
- [ ] omarchy video_class lua: add `nuvio-linux` literal (VRR/no_dim/idle rules apply to Qt window).
- [ ] Final packaged desktop-file name / app_id confirmation at release-planning time.

### Gated behind P1/P2 (by design, not started)
### Phase 1.5 — PlaybackSession wiring (DONE 2026-08-27)
- [x] `StreamResolver.isComplete(type,id)` — QML/caller-visible completeness so cache-hit requests decide synchronously (resolve() is silent for fully-cached keys; without this a repeat card click dead-ended).
- [x] `PlaybackSession` (systems/playback): requestPlay(type,imdbId,title) → playbackReady(title,url) | playbackUnavailable(title). Pending-key guard drops late completions for superseded clicks; decides from stream title with card-title fallback; unavailable NEVER clobbers an existing valid session.
- [x] Wiring: main.cpp context prop `playback`; MainShell Connections pushes "video" + launchMedia(url) (smoke-gated); LibraryPage card click → requestPlay(shelfInfo.type …) — series/anime rails now resolve their REAL type, not hardcoded "movie" — plus theme-styled no-source toast.
- [x] Tests: nuvio_playback_session suite (5 scenarios: cache-hit sync, in-flight completion, torrent-only honesty, stale-guard, zero-addons); StreamResolverTests +isComplete block. ctest 9/9; smoke gate PASS(advancing) post-change (vaapi-copy, glFrames flowing).
- NEXT QUEUE ITEM: torrent/P2P engine phase per plan §8.

### Phase 3.5 — Torrent/P2P engine slice (DONE 2026-08-27, plan §8 P3 first leg)
- [x] `systems/p2p` module: TorrServerProtocol (pure wire/policy layer: request bodies, stats parsing, magnet canonicalization with Compose test vectors incl v2 btmh, 7-rule file-index precedence, GET-modify-POST settings merge), TorrServerProcess (port 8091, /echo health poll 15s deadline, orphan shutdown before spawn, /shutdown→SIGTERM→SIGKILL), P2pEngine (generation-token state machine: binary up → add → metadata wait 1s×15 w/ id-1 fallback parity → streamReady; 1Hz stats; drop + 60s idle binary stop). ALL engine emissions QUEUED (no re-entrant callbacks; token bookkeeping race-free).
- [x] Resolver policy flip to two tiers: torrent entries now RETAINED; bestFor unchanged (honest empty without direct); new bestTorrent() exposes first hash in addon order.
- [x] PlaybackSession tier-2 routing: torrent-only keys → P2pEngine.startStream; relayed queued ready/failed back as its own terminal signals — QML/callers stay P2P-unaware. Binary absence = honest unavailable toast, never phantom playable.
- [x] Tests: nuvio_p2p_protocol suite (magnet vectors, bodies, stats parse, precedence chain, settings merge); resolver/session suites extended. ctest 10/10; smoke PASS(advancing) post-change.
- [x] FOLLOW-UP (same day, commit 94e831d9): torrent cache-size plumb COMPLETE — AppSettings `torrentCacheSize` property in separate `torrent_settings` store under Compose-parity key `cache_size` (enum names NONE|GB_2|GB_5|GB_10, default GB_2, invalid values fall back); pure `toTorrServerCacheMb()` mapping in protocol layer (64/2048/5120/10240) + tests; P2pEngine.setCacheSizeProvider consulted per-start AFTER binary up and BEFORE torrent add (getJson/requestWait refactor shared with postJson); SettingsPage combo; VideoPage torrent telemetry badge (statsUpdated relay, 3s staleness fade, speed/buffer%/seeders/peers — display-layer only); resolveServerBinaryPath gains repo-LFS candidate `composeApp/src/desktopMain/resources/torrserver/linux-amd64/TorrServer` so plain dev launches can use torrents with zero setup. ctest 10/10; smoke PASS(advancing).
- [ ] NEXT: binary bundling for packaging (Qt-line build scripts), stream-source picker UI later (P3 chrome parity), user visual pass on torrent playback end-to-end.

### Phase 6 — Meta detail route (DONE 2026-08-27, commit fce14130, USER APPROVED)
- [x] `MetaService` (systems/library): Cinemeta /meta/{type}/{id}.json fetch + pure normalization (modern tt:S:E AND legacy ::season:X:episode:Y id shapes, numeric episode sort, string-or-object cast/genres, malformed-safe); instant title seeding while loading; stale-answer guard; NUVIO_CINEMETA_BASE override parity. New nuvio_meta suite.
- [x] `MetaPage.qml` route "meta": backdrop+poster+info block, ▶ Play for movies, Season combo + episode list for series/anime; composite ids ride resolver/session unchanged.
- [x] Toast relocated to MainShell level (surfaces on any route). LibraryPage card click → meta.load + push("meta").
- Next in queue: track preference auto-selection port.



Full media implementation, overlays/upstream parity, packaging all formats, Trakt/Simkl, P2P/TorrServer, in-app updater.

## Session log 2026-08-27 evening (user approved every slice; ALL COMMITTED, nothing pushed)

Commits on explore/qtqml: ff6991d7 playbackSession | b81a37c1 p2p engine | 94e831d9 p2p follow-ups | fce14130 meta detail route | 3481cf94 track auto-selection | 92bde3f8 volume persistence + buffered bar | 988e5799 discord rpc | 32f2d6f4 screensaver inhibit | 8afd0985 language UI combos | e8650264 trailer kernel slice1 | f85a5443 trailer network client + button | 6abb0493 MPRIS.

### Meta detail route (DONE, USER VERIFIED PLAYBACK fce14130)
`MetaService` (Cinemeta /meta/{type}/{id}.json; modern tt:S:E AND legacy ::season:X:episode:Y id parsing; instant title seeding; stale-answer guard; NUVIO_CINEMETA_BASE override) + `MetaPage.qml` route "meta" (backdrop/poster/info block, Play for movies, Season combo + episode list for series/anime) + shell-level no-source toast. Composite episode ids ride resolver/session unchanged. nuvio_meta suite added.

### Track preference auto-selection (DONE 3481cf94)
Pure kernel `systems/mpv/TrackSelection.{h,cpp}`: verbatim LanguageCodeAliases+LanguageNameAliases port, pt/es special cases, normalizeLanguageCode (ALIAS PATH LOWERCASES values - pt-BR becomes pt-br through aliases while named branches keep region case; matching is primary-subtag so casing never breaks), target chains honoring device/original/default/none/forced words (device leg = QLocale::system().uiLanguages()), subtitle forced plan + signs/songs heuristic inferForcedSubtitleTrack. Runtime `TrackAutoSelector`: FILE_LOADED reopens selection window, per-tier applied latches, explicit `set aid/sid` ONLY (never alang/slang). Settings: preferredAudioLanguage(default device)/preferredSubtitleLanguage(default none)/useForcedSubtitles(true) + SettingsPage combos (curated 14-code list; keys Qt-line-local pref_audio_lang/pref_sub_lang/use_forced_subs - upstream Compose stores these in an opaque blob, migrate spelling at P4).

### Player chrome bits (DONE, USER VERIFIED PLAYBACK 92bde3f8)
Volume persistence: store `nuvio_player_runtime`, key `volume_level` float 0..1, 250ms debounce (Compose DesktopPlayerVolumeStorage parity; CROSS-LINE SHARE PROVEN LIVE - file written by Compose app was read by Qt build). Buffered-range seek bar via new MpvQuickItem.cacheSeconds property.

### P3 integrations leg (DONE 988e5799 / 32f2d6f4 / 6abb0493)
- Discord RPC `DiscordRpc.{h,cpp}`: frame codec w/ 64k corrupt-length guard (pure+tested), socket candidate chain XDG_RUNTIME_DIR/discord-ipc-N + snap extras, backoff 1s->60s reset on healthy handshake (dead-socket DETECTION is mandatory - silent writes never recover), 800ms debounce latest-wins dedupe seek>4s rebuild timestamps paused drops clock. AppSettings.discordEnabled local key discord_enabled, live toggle. Server id 1532796978973638830, env NUVIO_DISCORD_CLIENT_ID override.
- ScreensaverInhibit systemd-inhibit leg only (portal/KDE legs wait for packaging): child = systemd-inhibit --what=sleep:idle sleep infinity held exactly while playing; driven by controller snapshot playing-state; idempotent; child death self-heals m_active.
- MPRIS `MprisService.h/cpp`: org.mpris.MediaPlayer2(.Player) as org.mpris.MediaPlayer2.nuviolinux. Transport (Play/Pause/Toggle/Stop/Seek/SetPosition/OpenUri), Metadata from PlaybackSession title + snapshot duration, Volume settable, Position us read. VERIFIED LIVE VIA GDBUS round-trips. CRITICAL GOTCHA: QDBusAbstractAdaptor forwards interfaces THROUGH its parent object - export plain QObject parents at "/" and "/org/mpris/MediaPlayer2", never the adaptors themselves. integrations now links Qt6::DBus + nuvio::mpv + nuvio::playback.

### Trailer kernel + network leg (DONE e8650264 / f85a5443; UNCOMMITTED-NO, both committed)
`systems/trailer/TrailerKernel`: verbatim innertube client table (visionos>android_vr>android>ios) + fallback API key, request body/header builders, streamingData->scored buckets (progressive/video/audio/hls; qualityLabel fallback; container pref mp4|m4a<webm), sortCandidates (score desc -> no-n -> container -> priority) + orderSeparate visionos-first partition, buildPlaybackSource 4-tier policy adaptive_separate > progressive > hls_last_resort > adaptive_video_only.
`TrailerResolver` (slice-2 network): walks chain POSTing youtubei/v1/player with fallback key (watch-page visitorData path PARKED = slice 3), bounded event-loop requests 20s, accumulates shared chains, emits trailerResolved(url,audioUrl)|trailerFailed(reason). MetaService normalizes Cinemeta trailers[] to {provider,key}. MetaPage Trailer button when youtube trailer exists. MainShell routes resolved url into SAME player page via launchMedia; failures toast. SLICE 3 GAPS: separate-audio plumb to launchMedia/mpv.play second arg, reachability probes w/ mn-host rotation, visitor-data/watch-config path, episode thumbs+overview in list, click-to-route in-flight indicator.
## Session log 2026-08-27 late — trailer slice 3 COMPLETE (4 commits, ALL COMMITTED, nothing pushed)

Commits on explore/qtqml: 47baa1a6 plumb | 066931f7 visitor+probes | 55c1e0c6 async resolver+pending | c71af08d episode thumbs.

1. **Separate-audio plumb everything** (47baa1a6): `VideoPage.launchMedia(source, audioUrl)` -> `mpv.play(source, audioUrl || "")`; `MainShell onTrailerResolved(url,audioUrl)` now forwards audioUrl (was dropped). Full chain re-verified: MpvQuickItem::play -> MpvController::loadFile sets `audio-file` + loadfile, pending-flush carries m_pendingAudio. 2-line diff.

2. **Visitor-data + mn host-rotation probes** (066931f7): kernel gains PURE `hostRotationCandidates(url)` (original first, then per-mn-server alternates: rrN--- prefix renumbered to index+1, sn- token swapped; deduped). **FOUND A REAL COMPOSE BUG replicating it:** Compose's `replaceFirst(sn-regex, server)` inserts the FULL server string (incl `.googlevideo.com`) into a host that already carries the domain -> every rrN--- alternate has a doubled domain. Masked there only because the original URL wins the parallel probe first by completion. WE BUILD VALID ALTERNATES (strip `.googlevideo.com` off the token before the swap) and document it. Resolver probes the chosen video+audio URLs sequentially-first-success (this resolver is a synchronous QEventLoop walk; the only dropped Compose subtlety is racing-for-fastest - correctness preserved). hostRotationCandidates unit-tested (3-candidate rrN--- case proves no doubled domain).

3. **Non-blocking resolver + pending indicator** (55c1e0c6): resolveForKey previously ran the WHOLE multi-second walk on the QML thread (blocked UI, violated gotcha #7 "no sync QEventLoop from QML entry points"). Now resolveForKey returns immediately, spawns a detached worker thread (std::thread) running the same synchronous walk with a worker-local QNetworkAccessManager, then `QMetaObject::invokeMethod(QueuedConnection)` marshals the terminal signal + `m_resolving=false` back on the QML thread. New `resolving` property + `resolvingChanged`. MetaPage Trailer button -> "Resolving…" + disabled while in flight. VisitorData cache now mutex-guarded (single-worker gate keeps it race-free). New offline nuvio_trailer_resolver suite (invalid-key path: trailerFailed sync + stays idle). ctest 16/16.

4. **Episode thumbnails + overview** (c71af08d): MetaService::normalizeVideo already extracted `thumb` + `description` (overview) per episode - the MetaPage delegate just didn't render them. Delegate now: 104x58 PreserveAspectCrop thumbnail (raw https URL via QML Image), name line, one-line elided overview snippet, play glyph.

FINDINGS WORTH REMEMBERING:
- Compose hostRotationCandidates (TrailerExtractionPlatform.kt resolveReachableUrlOrNull) has a doubled-`.googlevideo.com` alternate-host bug; original-first parallel probing masks it. Don't copy it verbatim.
- The Qt resolver's sync QEventLoop pattern was blocking the QML thread for seconds - ALWAYS route blocking network walks off the main thread (worker + QueuedConnection marshaling) per gotcha #7.
- `std::fprintf("%d", QStringList::size())` -> qsizetype = %lld; cast explicitly.



### NEXT QUEUE for the next agent (in order)
1. ~~Trailer slice 3~~ DONE 2026-08-27 (see session log above): audioUrl plumb, visitor-data fetch, mn host-rotation probes, non-blocking resolver + pending indicator, episode thumbs/overview. ctest 16/16, build clean.
2. ~~Library watch-state foundation~~ DONE 2026-08-27 late (commits a5231146 + eb925d01 + 91d6272a): `systems/watching` module — Compose-parity storage located FIRST (watch_progress.properties key `watch_progress_1` / watched.properties key `watched_1`, JSON StoredWatchProgressPayload/StoredWatchedPayload shapes), pure policy (buildProgressKey `tt_s1e1` / watchedKey `type:id:s:e` / 90% completion / freshness comparator / newest-per-key CW selection), WatchCodec (camelCase, encodeDefaults, null-omit, ignoreUnknownKeys), WatchingStore over PropertiesStore (byte-compatible, cross-instance), WatchRecorder (1s threshold, 10s debounce, complete→watched+resume-drop). Wired: PlaybackSession exposes currentType/currentId; MainShell beginSession (composite tt:S:E split) + resume lookup; VideoPage 1Hz pump + ≥90% complete + route-leave abandon; HomePage Continue-Watching rail with progress bars. **POLISH COMPLETE**: resume-position playback start (MpvItem.play 3rd arg → loadfile `start=+sec` options field — FIXED latent bug: mpv has no "play-start" option; recorder resumePositionMsFor identity-scoped resumable-only lookup + tests); watched badges (LibraryPage card ✓ overlay, MetaPage movie Play→"✓ Watched", episode list ▶→✓ per episode). **CROSS-LINE PARITY PROVEN LIVE: Qt codec decoded the user's real Compose-written watch_progress.properties (77 entries, correct keys incl `kitsu:48899_s1e1`)**. ctest 17/17; smoke PASS(advancing) re-verified after each commit.
3. ~~Player chrome remainder P2~~ DONE 2026-08-27 late #4 (commit 18853dde): chrome media title (VideoPage mediaTitle, episodes "S1 E2 · Title"), audio/sub track pickers (TrackInfo + `selected` flag, MpvQuickItem `tracks` QVariantList + `setTrack(kind,id)` explicit aid/sid, TransportBar Audio/Sub ✓-menus incl Off), screenshot parity resolved (Compose has no hotkey; FIXED our letter-key UPPERCASE bug that inverted mpv's case-sensitive `s`=screenshot / `S`=each-frame bindings — printable ASCII now forwards case-preserved). ctest 17/17, smoke PASS(advancing), keytest PASS 4/4.
4. P4 sync parity: STORAGE LEG DONE 2026-08-27 late #5 (see session log + AGENTS.md "AppSettings blob parity"): all player/discord/torrent settings now live in the Compose stores/keys/formats with one-time legacy migration, pinned by the rewritten nuvio_appsettings_tests suite (parity-key assertions + legacy-migration block). REMAINING for full P4: sweep remaining stores for divergences (addons/qt-addons.properties, auth tokens already shared via auth.properties), then remote-sync code + contract tests against production backend as release gate.
5. Re-validation matrix whenever user boots other machines: fractional scaling 1.25 re-check, NVIDIA RTX 3070 nvdec-copy leg, Intel iGPU vaapi leg. omarchy video_class lua literal nuvio-linux add (user-side).
6. Packaging cutover is P5 - not started; when there: portal helper reuse (composeApp/src/desktopMain/native/linux/portal_inhibit_helper.c) for flatpak inhibitor legs, TorrServer bundling in Qt packaging scripts, desktop file/app_id final naming (user decision).

### NEW GOTCHAS THIS SESSION (add to habits)
1. NEVER author escaped-quote C++ via bash heredocs - layered JSON/shell escaping silently corrupts \" sequences (broke DiscordRpc p1, MprisService cpp twice). Use the editor tool (passes verbatim) or python with assert-guards.
2. QJsonValue::toInt takes (int default) NOT (bool* ok) - gate with isDouble().
3. QDBusAbstractAdaptor forwards interfaces THROUGH its parent QObject; export parents at paths, never adaptors. Out-of-line adaptor defs must repeat const exactly (supportedUriSchemes const mismatch bit us).
4. std::tie cannot bind rvalues - qMakePair for compound comparators.
5. QOverload<int>::of(&QProcess::finished) wrong arity; use <int, QProcess::ExitStatus>.
6. Emit QUEUED wherever callers may connect after call start (P2pEngine/DiscordPresence precedent) - keeps token bookkeeping race-free and tests deterministic via processEvents.
7. QNAM sync helper pattern: requestWait bounded local QEventLoop (30s wall) only from internal continuations, never QML entry points.
8. Nested-class definitions must precede their first use inside the same cpp (AppSettings TorrentStore ordering).
9. When a header grows past ~2 substantive edits in one session, stop patching and rewrite it wholesale via editor - overlapping partial edits corrupt slot sections (DiscordRpc header incident).


## SESSION 2026-08-27 late #5 — P4 AppSettings blob-parity storage leg DONE
Scope (single commit; ctest 17/17, no main.cpp touch so smoke gate not re-run):
- AppSettings.cpp rewritten around the Compose contract (header comment documents every store/key/format): player_settings.properties gets preferred_audio_language_1 / preferred_subtitle_language_1 / subtitle_use_forced_subtitles_1 / stream_cache_size_1 (String enum MB_64|MB_256|MB_512|GB_1|GB_2) / decoder_priority_1 (Int 0/1/2); discord_settings.properties discord_enabled_1; torrent_settings.properties cache_size_1 (NOW profile-scoped; was unscoped legacy). Compose ground truth re-verified in PlayerSettingsRepository/PlayerSettingsStorage/PlayerLanguagePreferences/DiscordSettingsStorage/P2pSettingsStorage before writing a line.
- One-time migration on first getter read from the Qt-local legacy keys (settings/: pref_audio_lang, pref_sub_lang, use_forced_subs, stream_cache_size int-MB, decoder_mode, discord_enabled; torrent_settings/cache_size unscoped) -> parity keys; legacy keys removed only after successful port. Values survive across the rename (tested).
- Cache API stays int MB outward (slider unchanged); internally snapped so stored values are always valid StreamCacheSize names.
- Decoder BY DESIGN: "vaapi"/"nvdec" pins unrepresentable in Compose's 3-state int (0/1 share one hwdec chain via the vendor-gated auto logic) -> persist 0, read back "auto"; SettingsPage combo trimmed to [auto, software] (was a silent restart-flip trap). Behavior identical: PreferencesApplier auto = vendor-gated vaapi/nvdec chain anyway.
- theme_dark deliberately NOT migrated (Compose theme_settings is a color-theme enum CRIMSON..WHITE; no dark/light boolean exists).
- Tests rewritten wholesale after two failed partial edits (re-confirmed gotcha #9): defaults for ALL settings incl torrent/discord/lang prefs; signal-once semantics; software round-trip; parity-key assertions read straight from player_settings/discord_settings/torrent_settings properties files (decoder_priority_1==2, stream_cache_size_1==MB_64 etc.); full legacy-migration block seeding exact pre-P4 shapes and asserting value survival + parity-key creation + legacy removal + fresh-instance parity-only reads.
- NEW GOTCHA: PropertiesStore snapshots at construction - an instance built BEFORE another instance's writes never sees them (in-memory map, write-through, never re-reads). Post-migration assertions MUST construct fresh store views (this produced a false test failure mid-session). Also: partial edits on this test file corrupted a line (`const auto prio` deleted by overlapping old_text); when >2 edits accumulate, rewrite wholesale per gotcha #9.

## SESSION 2026-08-27 late #6 — P4 sweep: addons blob-parity DONE
Scope (ctest 17/17; no main.cpp functional change beyond enabled-filter):
- Store-divergence sweep executed FIRST: enumerated every Compose store name (DesktopStorage.store call sites, 45 stores) vs Qt's 8 PropertiesStore files. Only remaining LIVE divergence on both sides = addons (qt-addons.properties isolated by design since P1, header-flagged "Migration = P4 work item"). Theme deliberate divergence stands; search_history/library pins/continue_watching_* are Compose-only features not yet in Qt (parity when implemented); auth + volume + watch_progress/watched already shared.
- New pure layer systems/library/AddonStore.{h,cpp}: truth codecs for installed_addon_urls_1/addon_enabled_states_1 (byte shapes asserted literal vs kotlinx output), normalizeManifestUrl = verbatim port of Compose ensureManifestSuffix + normalizeManifestUrl + encodeUnsafeHttpUrlCharacters (scheme table incl stremio://, fragment strip, trimEnd('/'), /manifest.json before query re-append, unsafe-char percent-encode), sha256(url) manifest cache in qt-addons with legacy addon_<n> migration (bodies preserved via entry's own url field).
- AddonRegistry rewritten onto the contract: load() builds rows from truth urls (cached manifest or async-fetched placeholder rows with id=""; changed() per arrival); add() = normalize/dedupe/fetch/verify THEN persist urls+enabled(true)+cache atomically per store; remove() cleans truth urls + enabled entry + hash-cache row; setEnabled(index,bool) Q_INVOKABLE persisted (UI toggle = later task). Registry owns LONG-LIVED m_truth/m_cache PropertiesStore instances (snapshot-at-construction gotcha now applied to production design: per-call instances would lose concurrent writes). fetches NEVER block the QML thread (gotcha #7 respected).
- main.cpp syncResolverAddons skips enabled==false rows; StreamResolver.valid() independently rejects empty-id placeholder rows so double protection.
- Tests rewritten wholesale (reconfirmed gotcha #9 after partial-edit churn): normalization table (8 cases), byte-shape round-trips, compose-shaped hand-written cross-reads, legacy migration, placeholder/enabled-default semantics, parseManifest rejection paths. Two build fixes en route: nuvio::settings is NOT an enclosing namespace of nuvio::library (needs using-decl or qualification); unique_ptr<fwd-declared> needs out-of-line dtor (~AddonRegistry()=default) because implicit dtor instantiates deleter in foreign TUs (mocs_compilation).
- LIVE PARITY PROVEN: /tmp/addon_parity_probe (read-only getters only) decoded the user's real Compose-written ~/.config/nuvio-linux/addons.properties: DECODED_URLS=5, opensubtitles-v3 enabled=false, cinemeta/aio*/ultramax true, first-url normalize idempotent. Java-properties \\:-escaping handled by existing PropertiesCodec unchanged.
- REMAINING P4: Compose-only feature stores arrive with their features; server-side addon-sync push/pull (Supabase addons table) intentionally NOT ported yet - local-truth parity is this leg; remote sync = full-P4 task shared with multi-profile work.

## SESSION 2026-08-27 late #7 — P4 remote-sync foundation: device identity DONE
- New pure class systems/settings SyncIdentity.{h,cpp} = byte-exact port of Compose core/sync/SyncClientIdentity (+desktop storage): store sync_client_identity.properties key client_instance_id, prefix nuvio-mobile- (SERVER-FACING - never rebrand per AGENTS.md), +32 random [a-z0-9] via QRandomGenerator::system(); validity len 16..96 over [A-Za-z0-9_-]; invalid stored ids replaced not trusted. main.cpp ensures identity exists before any auth/sync traffic (qCInfo lcNuvioAppModules line). LIVE probe adopted the real Compose-written id verbatim (nuvio-mobile-abdub...zato). ctest 18/18 incl new nuvio_sync_identity suite.

### REMOTE-SYNC PROTOCOL MAP (Compose core/sync, verified 2026-08-27 - do NOT re-read all 1500 lines next time)
- TRANSPORT: Supabase postgrest RPCs ONLY TWO for settings: sync_pull_profile_settings_blob / sync_push_profile_settings_blob. Params: p_profile_id, p_platform (="desktop" per SyncPlatform.kt; home_catalog uses "home_catalog_shared"), p_origin_client_id (the client_instance_id above), pull also accepts p_settings_json-less response decode via SettingsBlobResponse(profile_id,...), push sends p_settings_json.
- BLOB: {"version":3,"features":{theme_settings JsonObject, poster_card_style_settings_payload String, card_depth_style_settings_payload String, player_settings JsonObject, stream_badge_settings JsonObject, debrid_settings JsonObject, tmdb_settings JsonObject, mdblist_settings JsonObject, meta_screen_settings_payload String, collection_mobile_settings_payload String, continue_watching_settings_payload String, trakt_settings_payload String, trakt_comments_settings JsonObject, notifications_settings {episode_release_alerts_enabled bool}}} (MobileProfileSettingsBlob v3). Values inside JsonObjects use the SyncPreferenceJson typed envelope {"type":"string|boolean|int|float|string_set","value":...}; string_set sorts before encode.
- DOMAIN NOTE: discord_settings + torrent_settings are NOT in the blob - Compose does not remote-sync them today (our P4 alignment future-proofs them; a Qt push leg must decide whether to ADD these keys server-side later).
- SEMANTICS: push on local change via observer signature dedup (buildSignature vs currentObservedStateSignature prevents echo loops); pull applies blob into storages then triggers observer push EXCEPT pending watch-source outbox drain first (restorePendingWatchProgressSource pattern: outbox -> apply -> observer push propagates).
- CREDENTIALS: ProviderCredentialSync separate RPC family with p_credentials param; credentials stripped from settings blobs by ProfileSettingsCredentialPolicy (withoutProfileCredentials) before export.
- NEXT LEGS (offline-first order): 1) ~~SyncPreferenceJson codec~~ DONE late #8 (nuvio_sync_prefs suite); 2) PLAYER-SETTINGS FEATURE PAYLOAD DONE late #8 (SyncPlayerSettings.h/.cpp over our five parity keys - literal envelope shapes asserted, field-wise merge verified incl invalid-entry + unknown-key tolerance). Blob assembly across the OTHER 12 features (theme/payload-string features) still pending their Qt stores either not existing or being deliberate divergences - assemble at RPC-client time; 3) ~~QNAM RPC client~~ DONE late #8 (SyncRpcClient in authsync: user-JWT bearer w/ anon fallback, rpcUrl on AuthConfig, accessToken accessor; offline-tested via QTcpServer fake asserting exact path/headers/body + success/anon-fallback/refusal paths; NOTE Qt title-cases header names vs OkHttp lowercase - RFC-case-insensitive so harmless). Test-side also fixed: fake reply must advertise true Content-Length or QNAM reports premature-close errors. 4) ~~pull/push orchestration~~ DONE late #8 (SyncOrchestrator; design decisions above implemented verbatim: pull apply-through-AppSettings + diff emits replacing reloadFromStore idea; skip-next + last-push double guard; partial player-only blob safe). Response root is a JSON ARRAY - SyncRpcClient now passes the full QJsonDocument. 5) ~~contract tests~~ DONE late #8 (SyncContractTests.cpp, gated NUVIO_SYNC_CONTRACT=1). LIVE RUN VERIFIED: pull anon -> 403 28000 valid-session-required; push short-param -> 404 PGRST202 whose hint CONFIRMS canonical signature (p_platform,p_profile_id,p_settings_json[,p_origin_client_id optional]). RESERVED PROFILE ID 900001 for all contract writes - never touch real user profiles in tests. Run: NUVIO_SYNC_CONTRACT=1 ctest -R nuvio_sync_contract --output-on-failure ; Tier1 adds NUVIO_SYNC_CONTRACT_EMAIL/_PASSWORD (full push->pull echo round-trip through PRODUCTION AuthService).

### LEG 4 DESIGN (orchestration - decided now, implement next session)
- PULL-ONCE-AT-STARTUP: in main.cpp AFTER AuthService exists + restoreSession() completed, IF configured+signed-in: blocking-with-timeout is BANNED (gotcha #7) -> instead construct AppSettings AFTER a queued pull completes OR accept stale-first-read + push corrected values to QML via existing changed() signals. RECOMMENDED: keep startup synchronous-free - do pull in background, applyRemotePayload(playerStore), THEN signal QML (AppSettings needs a reloadFromStore() that re-reads parity keys + emits all *Changed).
- PUSH PATH: hook orchestrator to AppSettings *Changed signals (already emitted per setter), debounce ~800ms (Compose observer cadence), build exportSyncPayload, skip when byte-identical to last-pushed (signature dedup via QCryptographicHash over compact json - mirrors Compose buildSignature intent), call kPushProfileBlob with p_profile_id=1/p_platform="desktop"/p_origin_client_id.
- 401 HANDLING: single silent retry after AuthService token refresh once refresh flow exists mid-session; until then surface as debug log only.
- REMAINING FEATURES GAP: blob v3 has 13 features; Qt exports only player_settings today. Compose applies per-key so partial is SAFE; theme uses replaceFromSyncPayload (whole-object replace) - DANGER: omitting theme_settings key entirely = no-op (safe) but sending EMPTY object would WIPE theme - never send empty JsonObject feature maps for keys Qt does not manage. Same caution for payload-string features ("" overwrites).

### BREADTH LEG B2 - WATCH-PROGRESS SYNC DONE (2026-08-27 late #9, bcca50ac + eee4b2cf + this commit)
- LATENT BUG FIXED first: WatchCodec::encodeProgress zeroed the sync envelope (cursor/dirty/initialized/lastPush) on EVERY Qt write -> wiped Compose repository bookkeeping in the shared store. Full-payload codecs now preserve it (encodeProgressPayload/decodeWatchedPayload etc.); entries-only overloads remain as legacy wrappers.
- ProgressSyncCodec: snake_case wire entries (explicit null season/episode), push/delete/cursor/delta/full-pull params, bare-number cursor parse (QJsonDocument cannot hold scalars -> SyncRpcClient.finished now ALSO carries rawBody; all consumers updated).
- WatchingStore: envelope API + dirty-marking upsert/remove/mark/unmark + upsertRemote (no dirty echo).
- ProgressSyncController (authsync): pushDirty splits dirty keys into push vs delete legs; fullSyncThenDeltas = initial full pull (newest-wins-remote merge, no dirty) -> cursor seeded from sync_get_watch_progress_delta_cursor HOLDING the in-flight slot -> later runs pull delta since cursor. Signed-out = silent no-op. FRESH WatchingStore per operation (read-after-write vs recorder instance).
- main.cpp: recorder resume/continueWatching signals -> onLocalProgressChanged (1.5s debounce); one fullSyncThenDeltas per session activation.
- OFFLINE-TESTED vs routed TCP fake (26/26): dirty-only push, clean-entry exclusion, dirty clear + lastPush stamp, delete leg path, initial merge + cursor seeding, signed-out zero-request. LIVE PROD RUN pending user creds (contract suite Tier-1 pattern applies; use RESERVED profile 900001).
- REMAINING sync-breadth: watched-items deltas (same family, shapes near-identical), addons rows (sync_push_addons + from(addons)), provider creds (blocked on Trakt/Simkl UI), 12 remaining blob features, multi-profile.


### BREADTH LEG B3 - WATCHED-ITEMS SYNC DONE (2026-08-27 late #9, commit 1c01685a)
- Wire shapes from SupabaseWatchedSyncAdapter: items {content_id, content_type, title (ALWAYS emitted, encodeDefaults default ""), season/episode explicit-null, watched_at}. Delete keys are OBJECTS {content_id, season, episode} with NO type on the wire - composite watchedKey strings parsed (type=first token, s/e=last two, id=middle-join).
- Paged full pull: p_page/p_page_size loop until short page (controller page size 200).
- Controller: onWatchedChanged debounce(1.5s) -> pushWatchedDirty (present items push / absent dirty keys -> delete-key objects); fullWatchedSyncThenDeltas = paged full pull merged newest-watchedAt-wins (upsertWatchedRemote, no dirty) -> watched cursor seeded from sync_get_watched_items_delta_cursor; later runs watched delta since cursor. Watched cursor/init fields live in the watched payload envelope (preserved by the B2 payload codecs).
- WatchRecorder: new watchedChanged() signal emitted after completion auto-mark; main.cpp wires it + session-activation full watched sync.
- Offline-tested via routed fake (26/26). REMAINING GAP: no manual mark/unmark QML toggle yet (recorder auto-mark only) - when the UI toggle lands it must go through the store + emit watchedChanged.

### BREADTH LEG A - SEARCH DONE (2026-08-27 late #9, commit 8bb045c9)
- CatalogService.search(q): percent-encode via QUrl::toPercentEncoding, parallel movie+series fetch("<type>", "search=<enc>"); search-catalog replies divert from rail shelves into dedicated typed lists (seq-token stale guard); parseMetas extracted as shared static (ingest + search both use it; drop-counter preserved).
- SyncRpcClient.h/.cpp FINALLY COMMITTED here - leg-4's QJsonDocument signal change had been left uncommitted (fresh-checkout breakage risk; lesson: git add -A nuvio-linux-qt/ for multi-module commits instead of enumerating).
- SearchHistory (settings): search_history_1 kotlinx List<String> byte-shape, most-recent-first + move-to-front dedupe, min 2 chars, cap 10, remove/clear; decode tolerant to garbage (Compose getOrNull parity). Wire-verified JSON literal assertions. GOTCHA RE-CONFIRMED twice this leg: (a) out-of-line dtor needed for unique_ptr<fwd-decl>; (b) stacked-namespace headers from repeated partial edits produce nested-namespace moc errors - when a NEW header needs >1 structural pass, write it in ONE editor call.
- SearchPage.qml: 350ms debounce + Enter submit, recent chips (per-item remove + clear-all) visible under 2 chars, Movies/Series typed grid sections (section type carried to meta.load), LibraryPage card idiom cloned; route "search" + Home nav button. live QQmlEngine wiring via searchHistory context property in main.cpp.
- USER-VISIBLE VERIFICATION PENDING: search against real network + click-through to MetaPage (user defers playback testing; flagged here).

### BREADTH LEG B - HERO AMBIENT TRAILER DONE (2026-08-27 late #9, commit b44a2f6d)
- Architecture validated FIRST: MpvQuickItem is controller-agnostic (Q_PROPERTY controller) and MpvController has zero singleton state -> SECOND mpv instance is the sanctioned Compose-parity hero approach (upstream runs concurrent surfaces; each = own mpv handle + render thread). main.cpp creates heroController eagerly, born-muted via setVolumePercent(0) (separate instance => can NEVER clobber user volume, unlike cycleMute). Kill switch NUVIO_NO_HERO=1 skips creation; heroAmbientEnabled context bool drives QML guards.
- TrailerResolver: resolveForKeyAmbient() = same worker, mode member picks ambientResolved/ambientFailed vs trailerResolved/trailerFailed at BOTH emit sites (sync-invalid + queued-finish); mode resets per call. Shell playback-route hijack therefore cannot fire for hero.
- MetaPage: MpvItem hero layer as FIRST child (z-by-declaration renders behind backdrop/info) + bottom-heavy scrim; autoplay when visible && trailer present; stops on route-leave/meta-switch/ambient-failure; backdrop Image hidden while hero runs; the existing full-playback Trailer button remains.
- QML gotcha NEW: signal handlers take UNTYPED params - function onX(var v) is a syntax error (Expected token ).) - the offscreen boot ritual caught it pre-commit, exactly as designed. BINARY LAUNCH CHECK IS NOW MANDATORY for any commit adding/changing QML (SearchPage qrc miss + this one both caught only at boot).
- USER-VISIBLE VERIFICATION PENDING: hero autoplay visual (muted trailer behind detail content, stop on back) on real session.

### BREADTH LEG C - MANUAL WATCHED TOGGLES + POSTER HOVER PREVIEWS DONE (2026-08-27 late #9, eda05452 + dfa65b18)
- WatchRecorder markWatched/unmarkWatched Q_INVOKABLES (marks drop resume row, Compose parity) + watchedChanged() signal -> watched-sync push/delete legs now cover MANUAL marks too. MetaPage: Play primary + Mark-watched action; watched titles show tap-to-undo.
- Poster hover previews: LibraryPage rail posters, 2s hover delay -> metaSvc.load (async) -> currentChanged -> first-youtube key -> TrailerResolver.resolveForKeyPreview (Mode enum Playback/Ambient/Preview replacing the ambient bool; resolver routing tests extended) -> previewResolved plays muted in a popup Rectangle over the card via the SHARED hero mpv instance (routes exclusive -> idle on library; NUVIO_NO_HERO disables previews too). Popup click opens detail. QML gotcha re-hit: signal handlers take UNTYPED params (var v is a syntax error).
- USER-VISIBLE VERIFICATION PENDING: hover popup video (resolution + muted playback) and hero/preview mutual exclusion on real session.

### AUDIT (2026-08-27 late #10, user-requested before continuing)
- git status CLEAN; 18 commits ahead of dev (8521a1a5..249d4178 era + gate fix), nothing pushed.
- CLEAN-SLATE REPRODUCIBILITY PROVEN: fresh configure+build in /tmp/nqt-clean -> 202/202 targets, 26/26 ctest in BOTH trees, offscreen boot clean from the clean binary.
- BUG FOUND BY THE AUDIT: ctest ENVIRONMENT passes empty-string vars -> qEnvironmentVariableIsSet returned true for an empty gate -> contract suite failed in trees without reachable local.properties. Fixed: gate treats empty as unset (SyncContractTests).
- STANDING RULE UPGRADED: after ANY commit, run (a) full ctest AND (b) an offscreen boot (6s, grep FATAL/unavailable) - incremental builds + qrc + QML syntax issues only surface via (b); fresh-dir builds catch missing CMake source entries.

### BREADTH LEG D - HOVER SETTINGS (2026-08-27 late #10, ca54dbed)
hoverPreviewEnabled (default true) + hoverPreviewDelayMs (default 2000, clamp 250..10000) as QT-LOCAL settings keys - Compose keeps hover prefs inside its opaque poster_card_style payload, no cross-line contract exists (documented divergence). LibraryPage hoverTimer/guard + requestPreview bound to them; SettingsPage Poster-hover section (switch + 0.5-5s slider). BUILD GOTCHA RE-HIT: duplicate signal declaration from overlapping partial edits (subtitleStyleChanged twice) - dedupe signal blocks when appending NOTIFY groups.

### BREADTH LEG E - STREAM AUTOPLAY OPTIONS (2026-08-27 late #10, 3acf59d8)
- AppSettings: streamAutoPlayMode (MANUAL default / FIRST_STREAM / REGEX_MATCH - Compose StreamAutoPlayMode enum names), streamAutoPlaySource (ALL_SOURCES default / INSTALLED_ADDONS_ONLY), streamAutoPlayTimeoutSeconds (default 3, snaps to Compose value list 0-10/15/20/25/30), streamAutoPlayRegex. Parity keys profile-scoped in player_settings.
- MetaPage: movies auto-start playback on detail open when mode != MANUAL (guarded to VISIBLE page - LibraryPage hover previews also mutate meta.current and would otherwise hijack playback from the library; series keep manual episode selection).
- QML GOTCHA re-confirmed: one handler per signal - two onCurChanged blocks in the same object = Property value set multiple times; merge into one handler calling both functions.
- DEFERRED (noted): p_selected_addons/plugins sets (no Qt UI for them yet), REGEX_MATCH picker semantics in StreamResolver (v1 picks best already), next-episode auto-play continuation (needs player-end continuation design), skip-intro/anime-skip (no Qt behavior).

## Watched full-pull pagination (2026-08-27 late #9)
`fullWatchedSyncThenDeltas()` initial leg now pages like Compose's `SupabaseWatchedSyncAdapter.pull`: `fetchWatchedPage(page)` recurses while rows==pageSize, accumulating into `m_watchAccum`; short page → `finishWatchedInitialMerge()` (newest-wins-remote merge, no dirty marks) → **cursor seed via the REAL `sync_get_watched_items_delta_cursor` RPC (p_profile_id only, bare-Long body) — verified against Compose source, the earlier 'bogus RPC' fear was wrong**. `parseCursor` codec + `setWatchedPageSize()` test hook added; `m_watchAccum` member on controller. FakeRpc serves sequential pages (FIFO `pullPages`, empty→legacy `pullReply` fallback) + routes the watched-cursor path. T3b: 2 full pages + short page → 3 rows applied, cursor seeded+initialized. 26/26 suites green, binary-freshness verified (stat mtime) after the stale-binary false alarm earlier in the segment. LESSON (repeat offender): when state is uncertain, READ the whole file before editing — two editor calls were burned on misremembered anchors, one on the WRONG FILE (SyncContractTests vs ProgressSyncControllerTests).

## P2P TorrServer packaging bundling (2026-08-27 late #10)
Final offline-proceedable p2p leg. app/CMakeLists.txt: `nuvio_torrserver_bundle` (ALL) copies the repo-LFS binary (composeApp/.../torrserver/linux-amd64/TorrServer, 61MB, 0755 preserved by copy_if_different) into ${CMAKE_BINARY_DIR}/native/torrserver/linux-amd64/; ELF-magic probe (file(READ LIMIT 4 HEX == 7f454c46)) skips git-lfs pointer files gracefully — deliberately NOT file(SIZE), that needs CMake 3.21 vs our 3.16 floor. install(BINDIR) under NUVIO_INSTALL when bundled. TorrServerProcess.resolveServerBinaryPath(): NEW CWD-independent app-dir candidates FIRST (applicationDirPath/TorrServer = packaged layout; applicationDirPath/../native/torrserver/linux-amd64/TorrServer = dev build bundle), then the old CWD list, cache-home last (was briefly orphaned in a 'tail' list during editing — caught before build). Resolution-order tests appended to nuvio_p2p_protocol (env override verbatim; chdir-sandbox CWD candidate with XDG_CACHE_HOME isolated; env vars saved/restored). Bundle verified on disk 61038754B 0755; 26/26 suites green; p2p binary freshness stat-verified.

## Queue decisions 2026-08-27 (user)
- **A1 Tier-1 live sync round-trip: DONE, PASS** (commit 42833404). Sign-in via production AuthService works; blob push→pull→echo exact on a THROWAWAY ACCOUNT PROFILE. KEY FINDING: profile ids are SERVER-VALIDATED account indexes 1..6 (Compose MAX_PROFILES) — the old 'reserved profile 900001' convention is WRONG (P0001 'Invalid profile id'); the suite now picks a free index via sync_pull_profiles, creates 'qt-contract-tmp' via sync_push_profiles, round-trips, then sync_delete_profile_data (cleanup 204 verified). p_origin_client_id must be a SyncIdentity-minted id (server accepts 4-param push; 'nuvio-mobile-contract-test' literal was fine shape-wise but we now mint via production path). Credentials NOT stored anywhere (env-only).
- Re-validation matrix (NVIDIA/Intel legs): DEFERRED by user until after P5 packaging, on jjdizz1l@x570omarchy via ssh.
- omarchy video_class lua item: REMOVED from queue (personal machine config, not project).
- B sweep ACTIVE: CW-preferences parity (storage codec + rail honoring + settings page), HomePage rail watched markers, AddonsPage enable toggles.
- C phases added to roadmap (P5 packaging cutover, Trakt/Simkl port, in-app updater, multi-profile).

## B1 scope: ContinueWatching preferences parity (Compose ground truth, located)
Store `continue_watching_preferences` (DesktopStorage), profile-scoped key `continue_watching_preferences_<profileId>`. Value = kotlinx JSON (encodeDefaults=true, ignoreUnknownKeys): isVisible=true; style enum NAME (Card|Wide|Poster); upNextFromFurthestEpisode=true; use_episode_thumbnails_in_cw=true; show_unaired_next_up=true; blur_continue_watching_next_up=false; dismissedNextUpKeys Set<String>→array; showResumePromptOnLaunch=true; sort_mode enum NAME (DEFAULT|STREAMING_STYLE|SPLIT_UPCOMING). Source: ContinueWatchingPreferencesRepository.kt (StoredContinueWatchingPreferences private — port field-for-field).

## B sweep COMPLETE (2026-08-27 late #11; 3 commits, 27/27 suites, boot-checked each)
- **B1 CW prefs** (20d8b5b4 codec+store, da3012a9 recorder+QML): WatchRecorder exposes `cwPrefs` QVariantMap property + 8 persisting setters (continue_watching_preferences store, Compose-parity); HomePage rail honors `visible`; SettingsPage gained a Continue Watching section (visible/style/sort/episode-thumbs/upNextFurthest/unaired/blur/resumePrompt). HONEST NOTE: style + sort modes + unaired/blur/upNext knobs have NO behavioral surface yet — Qt's CW model is resumable-entries-only (no next-up candidate subsystem; Compose's sort/dismiss semantics only act on next-up items). They persist for cross-line parity.
- **B2 watched markers** (6fe4af01): SearchPage result posters got the LibraryPage-style ✓ overlay (movie-level isWatched). FINDING: HomePage has NO content rails (only CW rail + nav buttons) — the 'home rail watched markers' gap DISSOLVED; markers now cover Library + Meta + Search = every content surface that exists.
- **B3 addons toggle** (61119a91): AddonsPage rows gained an enable Switch (wires the existing AddonRegistry.setEnabled Q_INVOKABLE; row dims when disabled).
- Boot-check discipline held: isolated launch + log grep for QRC/QML errors after each QML-touching commit (zero errors; pkill-in-compound-chain bite recurred once, per gotcha #1).

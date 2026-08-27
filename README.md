# Nuvio Linux — Qt Quick exploration line

P0 structural spike per `nuvio-linux-qt.md` (in-repo design doc). Compose app
remains the shipped product unless/until P0 gates pass.

## Requirements (portability charter)
CMake ≥3.16 · GCC ≥11 or Clang ≥13 · Qt ≥6.2 (Core Gui Quick QuickControls2)
· libmpv development files (`mpv/client.h`, `mpv/render.h`) · Ninja optional.
Baseline x86-64 ISA only — arch-specific flags are rejected at configure time.
Public Qt APIs only (no private headers); <=6.2-compatible patterns throughout.

## Build / test / run
```bash
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build/dev
ctest --test-dir build/dev
./build/dev/app/nuvio-linux-qt [file-or-url]
```

## Environment knobs
| var | meaning |
|---|---|
| `NUVIO_QT_SMOKE_URL` | headless gate: play file/URL, machine-readable PASS/FAIL |
| `NUVIO_QT_SMOKE_TIMEOUT` | gate ceiling seconds (default 25) |
| `NUVIO_MPV_DEBUG=1` | verbose libmpv message mirroring + categories |
| `NUVIO_MPV_HWDEC=<chain>` | override hwdec chain selection |
| `NUVIO_MPV_EXTRA_OPTS=k=v,...` | append raw mpv options (CI escapes) |
| `NUVIO_MPV_NO_AUTOFALLBACK=1` | disable sticky decode-fallback |

## Status (P0)
Structure enforced by link graph ✔ · mpv controller/renderer/item ✔ ·
policy/user-config/keymap units tested ✔ · shell+transport UI ✔ ·
snapshot-copy pipeline bug tracked in branch notes (automated FAIL until fixed).

# LENS — Progress

## Phase 1 — Scaffold

Status: code complete, headless tests pass. **The Mac checks (lipo, auval,
pluginval, Logic) have not been run yet.** Phase 1 was built in a Linux
cloud container, which can't build or validate an AU. Tag `phase-1` once
the Mac checks below pass.

### Built
- JUCE 8.0.15 (latest 8.x tag) as a git submodule at `lens/JUCE`.
  Catch2 v3.7.1 via FetchContent.
- CMake project: AU (aufx) + Standalone, x86_64 only, macOS 11.0, codes
  Asher / com.asher.lens / Ashr / Lens, COPY_PLUGIN_AFTER_BUILD.
- Buses: main mono→mono or stereo→stereo, plus an optional mono or stereo
  side-chain input.
- APVTS with all 513 parameters (9 global + 24 × 21 per band), version
  hint 1, grouped Global, Band 01 … Band 24. Bypass is the host bypass
  parameter. Tail length 1 s, latency 0.
- Audio path: pass-through.
- State and preset JSON (SPEC §10, §11): `format_version` 1 with an empty
  migration chain, preset metadata, all 24 bands, globals, both A/B slots,
  analyzer settings, workflow step, GUI scale. Presets store only enabled
  bands and never touch bypass or the A/B slot. Bad or unknown input is
  ignored, never crashes. Preset floats are rounded to 6 significant
  digits; plugin state keeps full precision.
- `tools/get_pluginval.sh`: downloads the macOS pluginval release.
- Test helper `Tests/helpers/OfflineRender`: renders a buffer at any
  sample rate with a fixed or variable block pattern and an optional
  side-chain; WAV read/write; bit-identical and difference checks.
- Benchmark `lens_bench`: prints CPU model and real-time factor.

### Results (Linux container, Release)
| Check | Result |
|---|---|
| Catch2 | 13 test cases, 20,769 assertions, all pass |
| Parameter count, IDs, defaults, ranges, groups | pass |
| Bus layouts (mono/stereo, side-chain off/mono/stereo; bad layouts refused) | pass |
| State round-trip (5 random seeds, two generations) | identical parameters |
| Bad/unknown/future-version state | no crash, ignored safely |
| Preset save → load → save | identical text |
| Pass-through bit-identical, blocks 1, 64, 517, 4096, variable; 44.1/48/96/192 kHz; mono and stereo; with and without side-chain | pass |
| Benchmark, pass-through, 48 kHz / 128 | 0.020% of one core (Intel Xeon 2.1 GHz, cloud) |
| `lipo -archs` = x86_64 | not run (needs Mac) |
| `auval -v aufx Lens Ashr` | not run (needs Mac) |
| pluginval strictness 10 (AU) | not run (needs Mac) |
| Logic loads it, Side Chain menu shows | not run (needs Mac) |

Record the development Mac's CPU model here after the first Mac benchmark.

### Known issues
- None found in the headless tests. AU-specific behaviour is unverified
  until the Mac checks run.
- The test files trigger `-Wfloat-equal` warnings from JUCE's recommended
  warning flags (exact comparisons are intended in those tests).

### Coach text needed
- None in Phase 1.

## Building on the Mac

Needs Xcode (or the Command Line Tools) and CMake 3.22+
(`brew install cmake ninja`).

```bash
cd lens
git submodule update --init --depth 1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/lens_tests
./build/lens_bench
```

The AU is copied to `~/Library/Audio/Plug-Ins/Components/LENS.component`.

```bash
lipo -archs ~/Library/Audio/Plug-Ins/Components/LENS.component/Contents/MacOS/LENS
auval -v aufx Lens Ashr
tools/get_pluginval.sh
tools/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate ~/Library/Audio/Plug-Ins/Components/LENS.component
```

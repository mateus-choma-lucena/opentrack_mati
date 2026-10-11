# OpenTrack Enhanced 2.1 release report

Completed locally on 2026-10-11. Windows 11 Home 10.0.26300; Intel i5-12600KF
(10 cores / 16 logical processors); MSVC 14.51, Release x64; Qt 6.10.3,
ONNX Runtime 1.23.2 and shared OpenCV 4.12.0. No commit or push was made.

## Delivered

- Native Qt wizard: readiness, centering, noise, four comfortable yaw/pitch holds,
  optional roll/translations, game selection, native spline comparison, selected-axis
  review/application and Completion. Live physical pose, keyboard mnemonics, progress,
  Back/Retry/Skip/Cancel/Finish and automatic loss retry are integrated.
- Recenter/new sessions invalidate measurements, including during unreliable tracking.
  Drafts remain unapplied until approval. Apply stops tracking and saves before
  Completion; ordinary and nested Smart Mapping save failures restore mappings.
  Failed INI writes also restore Qt's queued keys and support a later successful retry.
- Existing NN2, validation/recovery, optical flow, adaptive filtering, prediction,
  Amplification/Speed, INI profiles and output protocols are retained.
- Integrated [2.1 user guide](OPENTRACK-ENHANCED-2.1-GUIDE.md), updated calibration
  instructions, Windows package verification and a reusable ZIP packager.

## Build and regression evidence

| Check | Actual result |
| --- | --- |
| Release x64 build and install | Passed |
| Full application regression suite | **143/143 passed**, 0 failures/skips, 78.09 s |
| Standalone core build and suite | **90/90 passed**, 0 failures/skips, 3.37 s; these overlap the full suite |
| Wizard rendering/layout | 1080p, 100/125/150/200% scaling; labels and table headers fit, page content scrolls, navigation stays visible; PNGs visually inspected |
| Runtime verifier | 46 required files present; selected runtime PE files verified x64; all five ONNX models load and execute |
| Windows ZIP | 140 runtime/documentation files; every entry checked against source size; test/profiling executables and preview PNGs excluded |
| Extracted package launch/shutdown | Native application started with isolated portable settings and exited normally with code 0 after its native close event |
| Source whitespace check | `git diff --check` passed |

Coverage includes NN2 core, recovery/loss, optical flow, dynamic prediction, adaptive
filtering, native spline monotonicity/signs/endpoints, gain/Speed, calibration statistics,
wizard branches/cancel/recenter/loss/Back/Skip, save failure/retry, profile compatibility,
selected-axis/original rollback, real FreeTrack/TrackIR packet conversion, deterministic
camera reconnect and shutdown. The packet test uses the production producer with isolated
shared-memory names; it does not modify the installed TrackIR registry configuration.

The actual pipeline remains camera → NN2 validated processing → selected adaptive/filter
stage → prediction → native mapping → Amplification → Speed response → offset/inversion
→ protocol. Speed's warm reference path is intentional. Calibration publishes the accepted
physical observation before internal covariance/recovery/flow assistance, then applies
the existing camera correction, source selection, Center and input inversion. Tests verify
that filter/prediction/gain do not contaminate it and that runtime effective outputs match
the native preview. No inference, camera scheduling or protocol implementation was changed
in this integration stage.

## Performance measured locally

**Physical camera:** connected PS3 Eye (VID 1415 / PID 2000), production open-driver
backend, grayscale 640×480, requested and negotiated 75 FPS, 600 delivered frames per run.
No images were saved. Capture timing includes waiting and the backend's existing copy;
it is not sensor-to-display latency or a full tracking benchmark.

| Physical capture run | Delivered FPS | Interval P50/P95/P99, ms | Blocking get_frame P50/P95/P99, ms | Stop, ms |
| --- | ---: | --- | --- | ---: |
| First open | 73.144 | 13.251 / 15.496 / 27.144 | 13.249 / 15.492 / 27.139 | 117.717 |
| Stop/reopen | 73.700 | 13.321 / 15.105 / 25.444 | 13.320 / 15.104 / 25.443 | 117.701 |

Both stops left `is_open=false`; no camera helper/subprocess remained. The backend's
skipped-sequence counter was zero, but **26 USB payload warnings occurred**, so this
does not prove zero physical frame loss. Delivered rate was below the requested 75.
Capture helper CPU was 62.5 ms per run (0.763/0.769% of one core); this excludes the
separate camera subprocess and NN inference.

**Synthetic calibration operations:** 1,000 timing batches, 300 observations per active
batch with 75 Hz timestamps; producer numbers are batch-amortized, not individual clock
measurements. Statistics run on the UI side. No camera or inference cost is included.

| Operation | P50, µs | P95, µs | P99, µs |
| --- | ---: | ---: | ---: |
| Disabled queue push | 0.0003 | 0.0003 | 0.0003 |
| Active queue push | 0.0030 | 0.0057 | 0.0067 |
| Drain per observation | 0.0113 | 0.0897 | 0.1757 |
| Record per observation | 0.0247 | 0.0510 | 0.0913 |
| Stationary summary, 300 samples | 26.7 | 67.61 | 90.026 |

The real producer remains fixed-size and allocation/lock free; idle collection skips
coordinate work. A four-second 75 Hz paced probe reported 0 ms process CPU for both
disabled collection and recording, at Windows CPU counter granularity (15.625 ms);
that must not be interpreted as literally zero CPU. The native Fusion wizard's separate
4.5-second stationary synthetic probe used **62.5 ms / 1.390% of one core** for live
preview and **218.75 ms / 4.861%** while recording, including Qt/offscreen repaint and
event processing. Its 13 ms synthetic producer delivered 345/347 observations, about
77 Hz; it is not a physical-camera measurement. Short-run timings vary by host load.

**Actual ONNX model adapters on synthetic inputs:** CPU provider, optimized graph,
four intra-op threads, one inter-op thread, fixed grayscale frame and forced pose ROI;
3 warm-up and 40 measured iterations per configuration. All five models were profiled
at 1/2/4 threads and four graph optimization levels. This table reports the four-thread
optimized configuration, including adapter preprocessing, without a face-quality claim.

| Model | P50, ms | P95, ms | P99, ms |
| --- | ---: | ---: | ---: |
| head-localizer | 0.711 | 1.124 | 1.149 |
| head-pose-0.2-small | 2.768 | 3.219 | 3.290 |
| head-pose-0.2-big | 7.041 | 8.025 | 8.484 |
| head-pose-0.5-small | 2.496 | 2.889 | 2.920 |
| head-pose-0.5-big | 7.163 | 8.301 | 8.392 |

Fixed-shape adapter buffer addresses stayed stable. Forty samples give a limited tail
estimate; these results do not establish end-to-end 75 FPS tracking or display latency.

## Changed files and package compatibility

- Wizard/integration: `gui/calibration-dialog.{hpp,cpp}`, `gui/mapping-dialog.cpp`.
- Shared native comparison: new `gui/mapping-comparison.hpp`, `gui/smart-mapping-dialog.cpp`.
- Persistence: `enhanced/smart-mapping.hpp`.
- Tests/probes: `enhanced/tests/CMakeLists.txt`, `calibration-integration.cpp`, new
  `calibration-profile-benchmark.cpp`, `camera-profile.cpp`, `offscreen-1080p.json`.
- Package/CI: `tracker-neuralnet/CMakeLists.txt`, `cmake/opentrack-install.cmake`,
  `tools/verify-package.ps1`, new `tools/package-windows.ps1` and `tools/smoke-package.ps1`,
  `.github/workflows/cmake.yml`.
- Documentation: this report, the integrated guide, `AUTOMATIC-CALIBRATION.md`, `README.md`.
- Qt translation catalogs: `gui/lang/{de_DE,nl_NL,ru_RU,stub,zh_CN}.ts` regenerated;
  untranslated new strings use the English fallback.

The package includes Qt, ONNX Runtime, all five models, OpenCV DLLs, NN/filter/camera
plugins, native UI/resources, mapping libraries, 32/64-bit FreeTrack/NPClient support,
presets, translations, notices and documentation. CMake now exports the actual OpenCV
linkage in `doc/runtime-dependencies.json`: shared builds require DLLs; CI's static
OpenCV build does not. GitHub Actions uses the same verified ZIP packager. **No remote
GitHub Actions job or separate local static-OpenCV build was run.**

## Remaining limits and LMU test

USB payload warnings and sub-75 delivered FPS remain on this physical setup. No driver,
firmware, camera acquisition or scheduling change was made. Physical unplug/replug,
user comfort/ranges, and LMU/Assetto Corsa gameplay need a human test; automatic reconnect
uses deterministic faults, while the physical probe covers stop/reopen. New UI strings
await translation. The existing OpenCV 4.12 paper-tracker advisory remains unrelated
to this calibration/NeuralNet work.

Exact LMU sequence:

1. Back up the INI and close LMU/competing TrackIR software. Select NeuralNet input,
   Enhanced Adaptive Filter and **freetrack 2.0 Enhanced**; choose **Enable both** in its output settings.
2. Start 640×480/75 tracking with NN2 validation. Open Automatic calibration; record
   Center, four-second noise, then comfortable left/right/up/down holds. Skip optional axes.
3. Choose **Le Mans Ultimate GT3**, Balanced, yaw 65° and pitch 25° each side. Compare,
   check yaw/pitch, approve, Apply, Finish, restart tracking and Center normally.
4. Launch updated LMU with native TrackIR support; enter offline cockpit practice.
   While stationary test small glances, comfortable endpoints, signs/center and
   cover/uncover recovery. OpenTrack output should reach reviewed targets before game caps.
5. Drive a slow lap after the stationary checks. Lower virtual targets if needed;
   compare styles and adjust Speed separately. Use **Undo calibration / previous mapping**
   if uncomfortable. See the [guide's detailed LMU procedure](OPENTRACK-ENHANCED-2.1-GUIDE.md#exact-lmu-calibration-test),
   including [official LMU native support information](https://lemansultimate.com/lmu-adds-first-elms-content-and-custom-livery-upgrades/).

Local evidence: `build/release-tests.xml`, `build/release-regression.log`,
`build-enhanced/release-core-tests.xml`, `build/release-performance/{calibration.txt,wizard.txt,ps3eye-75.txt,inference.csv}`,
and `build/enhanced/tests/calibration-*-dpi-*.png`. Package:
`build/release/opentrack-enhanced-2.1-windows-x64.zip`.

Reproduce builds/tests with `tools\dev-command.bat cmake --build build --target install --parallel 8`
and `tools\dev-command.bat ctest --test-dir build --output-on-failure`.
Run the opt-in camera probe from `stage\enhanced-camera-profile.exe`; profiling tools
require the installed DLL paths (and the offscreen Qt platform/plugin path for GUI probes).
Package using `tools\package-windows.ps1 -Stage stage -Destination build\release\opentrack-enhanced-2.1-windows-x64.zip`.
The smoke script requires a separately extracted, writable portable test directory;
it sends a native close event only to the application process it launches.

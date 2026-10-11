# OpenTrack Enhanced completion report

**Windows x64 Release build/install succeeded. All 45 automated tests passed in 14.16
seconds.** Both real-pipeline cases also passed five consecutive runs (10/10, 27.77 seconds).
Build dependencies: Qt 6.10.3, OpenCV 4.12.0, ONNX Runtime 1.23.2, MSVC 19.51, C++20 and
OpenMP. NeuralNet, Enhanced Adaptive Filter and both Windows camera backends compiled.

## Mapping correction delivered

- Head Movement Controls now live solely in **Mapping**. Enable, 0–100% Amplification/Speed,
  Reset, global/six-axis overrides, effective gain and millisecond/Hz estimates are present.
  The existing six axis tabs, asymmetric curves and saved points remain editable.
- Amplification runs once after the actual curves and relative-translation/neck transform.
  **42% = 1x**. Main/asymmetric overlays use the runtime's gain and saturation function.
  Dashed original and orange adjusted curves show the steady-state target, including a
  visible saturation plateau. Fixed offsets and raw protocol fields remain unamplified.
- Speed runs a timestamp-aware response after target mapping. **55% bypasses it exactly**.
  Other values control convergence using the original filter as a parallel mapped reference.
  Adaptive filtering provides a guarded target before temporal smoothing, avoiding a second
  serial exponential pass. Prediction runs once from the original filtered pose; its horizon,
  limits and state are not driven by amplification or output response.
- Master/parameter changes blend over 120 ms. Loss/recenter reset or hold response state.
  Queued Qt refreshes block write-back, fixing stale settings after reload/rapid toggles.
  Center requests arriving during a tick now survive until processed, fixing a discovered
  request-discard race; a deterministic integration check covers this case.
- Existing profile keys are preserved. The master defaults off; neutral values preserve
  the original mapped response. The selected filter stays warm. Game protocol implementations
  are unchanged. Deploy all rebuilt modules together to match the additive plugin API.

The existing Enhanced capture, timestamps/latency quantiles, adaptive smoothing, camera
enumeration/controls/reconnection and bounded prediction remain. NeuralNet's Enhanced area
has the requested five tabs: Performance, Adaptive Smoothness, PS3 Eye Camera, Prediction
and Diagnostics. Diagnostics/CSV include mapped targets, six effective gains, cutoffs,
limits, raw/output poses and monotonic source/output timestamps.

## Executed validation and package

The 45 cases comprise 19 signal/timestamp/queue/recovery tests, 19 head-control tests,
three installed NeuralNet tests, one INI persistence test, two actual pipeline/Qt tests,
and one production FreeTrack packet test. Coverage includes nonlinear/asymmetric curves,
independent axes, gain/sign/offset consistency, saturation/reduced limits, output timing
at 30/60/75/120 FPS, turns/stops/reversals, loss/recenter, prediction/adaptive interaction,
transitions, legacy INI keys and Mapping save/cancel. Real models run against an injected
synthetic camera in serial/latest modes; missing-runtime failure and shutdown are checked.

The FreeTrack test compiles the original writer unchanged, redirects named memory for
isolation and reads its actual packet. It checks six mapped axes, mm/radian conversions,
signs and raw fields. It avoids registry/game initialization. This is software packet
validation, not a running NPClient/game-consumer test. Basic/advanced Mapping renders were
visually inspected, including the original points and saturation plateau.

The verifier passed **23 required runtime files**, including `modules/onnxruntime.dll`,
all five ONNX models, NeuralNet/adaptive/camera modules, Qt platform and C++/OpenMP/libusb
dependencies. The portable ZIP is regenerated from the installed stage using the existing
packaging exclusions for test executables and preview PNGs.

The working Windows GitHub Actions workflow and package verifier were unchanged by this
Mapping correction. Earlier Enhanced work had already added dependency/test/package checks.
Remote Actions was not run here. The source workspace has no Git metadata, so no commit
or PR is claimed.

## Limitations and acceptance

Physical PS3 Eye modes, real-face accuracy, driver controls/unplug recovery, actual games
and true camera-to-display latency remain unvalidated. Host processing latency is measured;
response time constants are model estimates. Non-exponential third-party filter response
uses a last-useful/12 Hz baseline away from neutral. Faster nonneutral Speed values converge
faster up to the cutoff cap; an original passthrough at neutral already has zero response lag.

NeuralNet provides no calibrated confidence score. DirectShow enumerates advertised discrete
intervals, not all continuous rates. Backend sequence gaps do not measure all sensor/USB
drops. The bundled PS3 open backend advertises VGA 60/75 and QVGA 100/125, not exact QVGA
120; 120 is listed only for a backend that advertises it. No USB driver is replaced.

Hardware acceptance: compare controls off against 42%/55%; test gain endpoints and each axis
against the orange target in a FreeTrack/TrackIR-compatible game; compare Speed 0/55/100
with capture, smoothing and prediction fixed; exercise asymmetric curves, inversion, offsets,
recenter and face loss; repeat at advertised camera modes and export diagnostics. Detailed
steps and formulas are in [HEAD-MOVEMENT-CONTROLS.md](HEAD-MOVEMENT-CONTROLS.md).

Exact local commands, the full 16-step CI sequence, hardware checks and earlier Enhanced
source changes are in [ENHANCED.md](ENHANCED.md). Run **Actions → OpenTrack Windows x64
Full Build → Run workflow**, then download `opentrack-enhanced-windows-x64`.

## Files and deliverables

This correction changes `enhanced/{head-controls,head-controls-ui,motion,telemetry}.hpp`,
`api/plugin-api.hpp`, `logic/pipeline.{hpp,cpp}`, `filter-enhanced-adaptive/filter.{hpp,cpp}`,
`gui/mapping-dialog.{hpp,cpp}`, `spline/spline-widget.{hpp,cpp}`,
`gui/options-dialog.{hpp,cpp}`, `opentrack/main-window.cpp`,
`tracker-neuralnet/enhanced-controls.cpp`, tests and these Markdown guides. Tests changed:
`enhanced/tests/{CMakeLists.txt,head-controls.cpp,pipeline-controls.cpp,integration.cpp}`;
new files: `protocol-packet.cpp`, `isolated-freetrack.hpp`. Existing persistence tests remain.
Normal lupdate/lrelease also refreshes affected translation catalogs.

Local artifacts: `stage/opentrack.exe`, `opentrack-enhanced-windows-x64.zip`,
`build/enhanced-tests.xml`, and `stage/head-controls-mapping-{adaptive,fallback}[-advanced].png`.
The ZIP contains runtime files and manuals; UI previews/test executables remain outside it.

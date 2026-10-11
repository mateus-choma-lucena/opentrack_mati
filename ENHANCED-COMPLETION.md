# OpenTrack Enhanced completion report

**Local Windows x64 Release build: successful. Automated tests: 38/38 passed (13.97 seconds).**
Qt 6.10.3, OpenCV 4.12.0, ONNX Runtime 1.23.2 and MSVC 19.51 were used.
OpenMP, NeuralNet, the PS3 Eye backend and Enhanced Adaptive Filter compiled successfully.

Implemented software includes bounded latest-frame capture, stale-frame rejection, monotonic
pose timestamps, measured diagnostics and latency quantiles, CSV export, adaptive smoothing,
bounded prediction after filtering, camera mode/USB enumeration, exposure/gain controls,
automatic reconnection, additive profile settings and six Enhanced Qt tabs. Behavior-changing
features remain disabled by default; the previous capture path and all existing filters remain.

Head Movement Controls are available in global Options for every tracker and in NeuralNet's
Enhanced panel. They provide a master switch, live 0–100% Amplification/Speed sliders, neutral
Reset and optional six-axis overrides. **42% Amplification = 1x gain; 55% Speed = original response.**
Gain is applied once after response curves; it leaves fixed offsets and raw protocol fields intact.
Speed uses a per-sample shared adaptive-filter multiplier or an explicit fallback that replaces
other filters' output away from neutral, keeping their state warm. Prediction horizon is unchanged.
Settings/master changes blend over 120 ms to avoid discontinuous camera jumps.
Live diagnostics and CSV include effective gains/response, raw/output poses, host timestamps and
explicitly labeled estimated smoothing time constants. The exact formulas, pipeline and fallback
contract are in [HEAD-MOVEMENT-CONTROLS.md](HEAD-MOVEMENT-CONTROLS.md).

Packaging installs ONNX Runtime at `modules/onnxruntime.dll`, all five models, Qt/platform,
OpenMP, libusb and Visual C++ runtime DLLs. Missing runtime initialization and destruction are
tested to return an error without aborting. The package verifier checks 23 required files,
including the new Head Movement Controls documentation.

The automated suite covers timestamps, telemetry/first-protocol-submission timing,
percentiles, concurrent latest-frame delivery, shared-frame locking, stale rejection,
stationary jitter, fast turns, angular wraparound, frame-rate independence, spike and
persistent-outlier recovery, invalid measurements, confidence attenuation, prediction limits,
direction reversal, tracking loss, recenter, retry backoff and actual INI persistence.
Installed-plugin tests run real ONNX models against a synthetic camera in both serial and
latest-frame modes, inject/recover from disconnection, check invalid poses and joined shutdown,
render every Enhanced settings tab, and test missing-runtime failure in an isolated process.
The 13 additional head-control cases cover mappings, neutral configuration, time-aware Speed,
variable intervals, quick turns/stops/reversals, recenter, prediction/smoothing interaction,
transitions, endpoints and diagnostics. Two real-pipeline cases use the installed adaptive plugin
and a legacy filter to check nonlinear curves, all six axes, fixed offsets, smooth gain changes,
recenter/zero, prediction and Qt settings. Persistence tests also preserve preexisting INI keys.
Basic, advanced and live-diagnostics UI renders were visually inspected.

Partial/unvalidated items: physical PS3 Eye modes, human-face accuracy, driver-specific controls
and reconnection behavior, and true camera-to-display latency. NeuralNet does not supply a
calibrated confidence score; optional confidence handling is ready for trackers that do.
Actual FreeTrack/TrackIR consumers still require the hardware acceptance procedure. Their
protocol implementation/layout remains unchanged. Legacy trackers without acquisition metadata
use host poll timestamps; NeuralNet provides actual host frame-receipt timestamps.
DirectShow enumeration lists advertised discrete nominal intervals, not every continuous rate.
Backend sequence gaps cover the current PS3 subprocess connection, not all sensor/USB drops.

The open driver advertises VGA 60/75 and QVGA 100/125, but **not exact QVGA 120 FPS**.
120 FPS is only eligible when an installed backend actually advertises it. No drivers are replaced.
The driver's [rate table](https://github.com/opentrack/PS3EYEDriver/blob/master/ps3eye.cpp) is the source of the open-backend mode catalog.

Changed source files, remaining limits, hardware testing instructions, exact local commands and
the complete 16-step GitHub Actions sequence are in [ENHANCED.md](ENHANCED.md).
The workflow is [.github/workflows/cmake.yml](.github/workflows/cmake.yml): push to `master`
or run **Actions → OpenTrack Windows x64 Full Build → Run workflow**, then download
`opentrack-enhanced-windows-x64`. Remote GitHub Actions has not been executed in this session.

The supplied source workspace had no Git metadata; no commits or PRs were created.
Deploy the rebuilt plugin DLLs and PS3 subprocess together; profiles remain compatible.
Local deliverables are `stage/opentrack.exe`, `opentrack-enhanced-windows-x64.zip`,
`build/enhanced-tests.xml`, `stage/enhanced-settings-preview-0.png` through `-5.png`,
and `stage/head-controls-global-{adaptive,fallback}*.png`.

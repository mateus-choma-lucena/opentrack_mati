# Head Movement Controls

Open **Options → Head Movement Controls**, or **NeuralNet settings → Enhanced → Head Movement Controls**.
Both panels edit the same per-profile settings. The master switch is off in existing and new profiles.
The displayed defaults, **Amplification 42% and Speed 55%**, are neutral: first activation preserves
the existing tracking output, including its selected filter, response curves and prediction settings.
These numbers are UI reference points; the mappings below are independently defined for this fork.

## Amplification

Amplification scales the six mapped virtual camera movements, in API order X/Y/Z/yaw/pitch/roll.
Translations retain centimeters and rotations retain degrees. It does not scale the tracker pose,
modify curve control points, change smoothing or change the prediction horizon.

For percentage `p`, clamped to 0–100, the gain is:

```text
p ≤ 42: gain = 2^((p - 42) * 2 / 42)
p > 42: gain = 2^((p - 42) * 2 / 58)
```

| Amplification | Gain | Result of an existing 20-degree mapped yaw |
|---|---:|---:|
| 0% | 0.25× | 5 degrees |
| 42% | 1× | 20 degrees |
| 100% | 4× | 80 degrees |

The logarithmic mapping keeps low values useful and provides fine adjustment near neutral.
Advanced axis sliders **replace** the global gain on their axis, rather than multiply it.
They default to 42% each. The global slider is disabled while axis overrides are selected;
diagnostics display all effective axis gains.

Gain runs **once after all response curves and relative-translation/neck transforms**, and before
the existing hold/zero handling, fixed output offsets, final axis inversions and protocol submission.
Thus a nonlinear curve still evaluates the same physical movement; only its resulting virtual
movement is scaled. A fixed zero-position offset is not amplified. Existing curves may already
amplify movement: this control deliberately scales their result; there is no second application
of the new gain in the tracker, filter, predictor, or protocol. Protocol raw-pose fields are unchanged.

## Speed and the filter interface

Speed changes temporal responsiveness, never movement amplitude or camera capture rate.
The shared response multiplier for clamped percentage `s` is:

```text
s ≤ 55: multiplier = 2^((s - 55) * 3 / 55)
s > 55: multiplier = 5^((s - 55) / 45)
```

Its anchors are 0% = 0.125×, 55% = 1× and 100% = 5×. Higher values shorten smoothing response;
all response updates use actual monotonic acquisition intervals, and repeated output polls do
not create new measurements. No extra frame queue, sleep or busy-wait is introduced.
NeuralNet supplies the actual host frame-receipt timestamp. Legacy trackers that do not implement
acquisition metadata use the existing `data_with_info()` host poll timestamp; their response is
time-aware, but the application cannot infer the sensor's sampling cadence from that interface.

**Enabled Enhanced Adaptive Filter:** the pipeline calls the explicit shared interface
`IFilter::supports_head_response()` and `filter_sample_with_response(input, output, sample, multiplier)`.
The plugin scales its existing per-axis adaptive cutoff before its single exponential step,
with a 60 Hz cutoff cap. Saved rotation/translation smoothing, responsiveness, jitter and confidence
settings remain untouched. There is no additional Speed filter pass. At 55% the multiplier is
exactly one, so the original adaptive response is preserved. Quick-turn adaptation, spike rejection,
loss recovery and recenter resets remain active. Very fast movement may already reach the cutoff
cap at several Speed values; those values then have the same bounded response.

**Another filter, no filter, or disabled Enhanced Adaptive Filter:** at 55% the selected filter's
output is used exactly. Away from 55%, Speed takes responsibility for temporal response: it uses
the centered, unfiltered pose in a dedicated response stage, replacing the selected filter's
output rather than smoothing it again. The selected filter still receives every original call,
so its state remains ready for a return to neutral or disabling. Its internal state/settings are
not altered. The fallback cutoff per axis is:

```text
velocity = exponential average at 8 Hz of the raw per-axis velocity
cutoff = clamp((12 + 0.15 * abs(velocity)) * multiplier, 1.5, 60) Hz
alpha = 1 - exp(-2π * cutoff * actual_frame_dt)
output += alpha * shortest_axis_delta(input, output)
```

Stationary cutoff is 1.5–60 Hz; intentional turns raise it, even at low Speed. Each step is a
convex interpolation, avoiding filter overshoot/oscillation. Angular differences use the short
path across ±180 degrees. Gaps exceeding 250 ms suspend velocity estimation and preserve the
current output for the first returning measurement. Tracking loss and recenter reset motion state.
The first fallback sample is seeded from the current selected-filter output, avoiding a handoff jump.

An arbitrary third-party filter has unknown response. Neutral preserves it; moving away from
neutral adopts the documented fallback response. Faster/slower guarantees compare values within
that fallback, not its latency against every possible third-party filter. The UI states this behavior.
NeuralNet's optional internal uncertainty/noise filter remains explicitly configured by the user;
disable it manually when measuring the response of a single external smoothing stage.

## Pipeline, transitions and reset

The actual processing order is:

```text
tracker / NeuralNet pose + host acquisition timestamp
→ axis selection, camera-offset transform, centering/recentering
→ selected filter (shared adaptive Speed override), or Speed fallback
→ bounded optional prediction
→ existing rotation curves
→ existing relative translation / neck transform
→ existing translation curves
→ Amplification once on all six mapped movements
→ hold/zero, fixed offsets, final inversion
→ unchanged protocol pose/raw submission
```

Prediction receives the responsive pose and its original acquisition timestamp; Speed never
changes its configured horizon or caps. Prediction is not fed amplified data, and repeated
protocol polls cannot accumulate predicted offsets. Centering precedes both controls and resets
the fallback and predictor; an amplified centered zero is still zero.

Setting changes—including master on/off, global/axis changes, and Reset—blend their control
parameters using a monotonic 120 ms cubic smoothstep. Fallback handoffs use the same transition.
The first output at a changed setting retains the old effective controls. The transition has
zero endpoint slope, reaches its target exactly, and does not introduce steady-state extra pose
smoothing or extra buffering. Master off restores the previous pipeline after that bounded
transition. An instantaneous nonneutral gain removal would mathematically move a stationary
camera abruptly, so the 120 ms return deliberately prevents that jump. Existing tracking hold,
tracking-loss hold and zero-output actions keep their original meaning.

**Reset to neutral** restores 42%, 55%, disables axis overrides, and resets all six axis sliders
to 42%. It keeps the master switch's current state. OK saves and Cancel restores the saved values
through the existing options bundles; no old keys or profile files are migrated.

## Diagnostics and validation

Live diagnostics in global Options work for every tracker. NeuralNet's Enhanced Diagnostics and
CSV snapshot export also include enabled/transition state, configured percentages, effective
global/axis gains, effective response multiplier, active response path, raw and protocol poses,
source capture and output submission timestamps, and output validity. Timestamps are monotonic
host milliseconds, not wall-clock time. Formatting and CSV generation happen in the UI, not the
tracking hot path; the pipeline publishes a fixed-size snapshot without allocations or sorting.

The reported smoothing time constant, `1000 / (2π * minimum_axis_cutoff)` milliseconds, is a
**model estimate** of the slowest current axis. It is not measured physical response lag or
camera-to-display latency. The existing capture-to-protocol timing remains a separate **measured**
host processing metric. Unknown third-party filter response is N/A.

Automated coverage includes gain mapping and all six axis overrides; neutral/disabled identity;
Speed response; variable, repeated and regressed sample times; quick turns, stops and reversals;
recenter; prediction bounds/horizon; adaptive sharing without a second pass; enable/disable
transitions; both slider endpoints; diagnostic fields; and actual INI save/reload. Real pipeline
tests exercise installed adaptive and fallback filters with nonlinear curves, fixed offsets,
six-axis output, smooth gain changes, tracking recenter/zero, prediction and global Qt controls.
NeuralNet integration tests continue to exercise serial/latest-frame capture with real ONNX models,
reconnect and missing-runtime recovery; all six Enhanced tabs are rendered.

Build/test/package commands and CI steps remain in [ENHANCED.md](ENHANCED.md). Deploy all newly
built modules with the application because the additive plugin interface requires a matching build.
No USB driver changes are needed. FreeTrack/TrackIR protocol schemas and implementations are unchanged.

Physical acceptance still requires a PS3 Eye or another camera and an actual output consumer:

1. Load an existing profile with controls off; record its pose output and responsiveness.
2. Enable the neutral 42%/55% configuration; verify that the view stays at the same position.
3. Hold a visible fixed pose while enabling/disabling a nonneutral gain; confirm a smooth 120 ms
   transition without a discontinuous view jump. Check 0%, 100% and each axis override.
4. With Enhanced Adaptive Filter enabled, compare Speed 0/55/100 during slow motion, quick
   turns, stops and direction reversals. Compare the fallback with another selected filter.
5. Repeat with NeuralNet internal smoothing off/on explicitly and prediction off/on at a fixed
   horizon. Recenter at nonneutral gain and verify the expected center and fixed offset.
6. Check FreeTrack 2.0 Enhanced and TrackIR-compatible consumers. Capture diagnostics CSV,
   confirm pose/timestamp/estimated fields, and separately measure display latency if required.

Camera hardware, real-face accuracy, physical latency, and game integration have not been
validated by the synthetic software tests. No hardware-performance claims are inferred from them.

Source changes for this feature are `enhanced/head-controls.hpp`, `enhanced/head-controls-ui.hpp`,
`enhanced/options.hpp`, `enhanced/motion.hpp`, `enhanced/telemetry.hpp`, `api/plugin-api.hpp`,
`logic/pipeline.{hpp,cpp}`, `filter-enhanced-adaptive/filter.{hpp,cpp}`,
`gui/options-dialog.{hpp,cpp}`, `opentrack/main-window.cpp`,
`tracker-neuralnet/{enhanced-controls.cpp,ftnoir_tracker_neuralnet.h,ftnoir_tracker_neuralnet.cpp}`,
`enhanced/tests/{CMakeLists.txt,head-controls.cpp,pipeline-controls.cpp,settings.cpp,integration.cpp}`,
`tools/verify-package.ps1`, `.github/workflows/cmake.yml`, `cmake/opentrack-install.cmake`,
`README.md`, `ENHANCED.md`, `ENHANCED-COMPLETION.md` and this manual. Normal lupdate regeneration
also refreshes the existing GUI and tracker translation catalogs.

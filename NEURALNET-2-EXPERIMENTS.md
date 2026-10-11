# NeuralNet 2.0 experiments

Optical Flow, Dynamic Prediction 2.0, and the Live Analyzer are independently
switchable and disabled by default. Existing profiles retain the legacy pipeline
when NeuralNet 2.0 and these switches are off. Existing manual prediction remains
available. No models, camera drivers, firmware, mapping curves, or gain/speed
controls are replaced. No GPU provider is enabled.

## Optical flow and fusion

The capture owner runs sparse pyramidal Lucas–Kanade on grayscale images reduced
to 320 pixels wide (400 for Accurate), using 32/48/80 facial-interior corners.
It uses the last trusted NeuralNet face box, an inset elliptical mask, two-way
tracking, photometric error, 0.8 pixel forward/backward error, a median motion and
bounded residual gate. At least eight points, 65% agreement, and horizontal and
vertical feature coverage are required. Vectors are capped at 600 analysis
pixels/s and 20 pixels per observation. Drift is capped at eight analysis pixels.
Features are reseeded at most every 250 ms and whenever tracking is unreliable.

A face anchor expires after 50 ms. Loss, disconnection, recenter, format/quality
changes, reversed timestamps and capture gaps reset the flow state. A fresh
trusted absolute pose resets accumulated drift. Inference exchanges only a small
anchor with the capture owner; it never waits for Lucas–Kanade computation. Flow
does consume capture-thread CPU when enabled, and its cost is exposed in diagnostics.
Image/pyramid sizes and feature vectors are bounded. OpenCV can allocate scratch
storage; this is not an allocation-free algorithm.

Image motion does **not** reconstruct head rotation or translation. Fusion is
only a reliability-weighted inhibition of the existing prediction offset when
facial image features appear stationary despite predicted movement. It can reduce
that offset by at most 30% × Fusion Strength × flow reliability. It cannot add
motion or drive an axis direction. Fast moving features leave prediction unchanged.
Flow must occur after, and belong to, the currently published absolute pose. A
new NeuralNet pose, inconsistent/occluded features, stale data, hold, recovery, or
loss immediately restores the standard path. There is one predictor and no
integrated optical pose. Fusion Strength zero is an exact fallback.

Flow alone does not change pose output without manual or dynamic prediction.
At a synchronous 75 capture / 75 pose cadence, every observation has a new
absolute pose and there is no applicable flow correction. The benchmark checks
that explicitly. The experiment remains off by default. A coherent moving
occluder inside the face region cannot always be distinguished from the face;
real-camera quality improvement has not been established.

Algorithm reference: [OpenCV optical-flow API](https://docs.opencv.org/4.13.0/dc/d6b/group__video__track.html).
The Windows build here uses OpenCV 4.12.

## Dynamic prediction

The horizon is the measured age since the acquisition timestamp, capped at the
configured maximum (0–15 ms), attenuated for slow movement. Existing velocity
noise protection remains; estimated reliability scales the offset. Immediate
velocity stops/reversals and NeuralNet motion classification suppress prediction.
The existing angular/translation caps still apply. Loss/holds/recovery reset
prediction eligibility. Each output poll computes from the measured filtered
pose, never from a previous extrapolated output. Maximum horizon zero returns
that pose exactly. Dynamic mode overrides the manual horizon while enabled;
switching it off preserves manual prediction. This changes output compensation,
not measured camera-to-display latency.

## Analyzer

Enhanced → Live Analyzer contains raw and validated yaw/pitch/roll, filtered and
predicted yaw/pitch/roll, estimated reliability, flow correction, capture intervals,
ONNX duration, and host capture-to-pose latency. Every graph labels units. Raw and
validated poses use tracker absolute coordinates; filtered/predicted poses use
centered coordinates before mappings, Amplification and Speed. Validation graphs
show the published validated/covariance/recovery stage, not a second smoother.
Blue/orange/purple identify yaw/pitch/roll. Lines break on invalidity, recenter,
and angular wrap.

The optional producer uses `try_lock`; graph/export contention drops telemetry
instead of blocking inference. The live ring holds 512 observations; recordings
hold at most 4096 observations and stop at the configured 1–50 second duration.
Consumers copy snapshots and draw/export outside the lock, at four UI updates/s.
Storage is allocated when enabled from the UI thread. A stopped recording is
retained until a new recording starts or tracking ends. Recordings replace the
latest output for each observation; repeated polls do not duplicate samples or
add to earlier extrapolated output. The output timestamp identifies that poll. Telemetry
drop counts include lock contention, invalid/nonmonotonic samples and unavailable
output associations; live-ring replacements are reported separately. Recording
capacity truncation is a stop, not an unreported sample drop.

Export Recording produces one CSV row per inference observation, with all six
axes at each stage, monotonic capture/validation/publication/output timestamps,
availability flags, reliability, timings, flow image units, flow correction,
center epoch and counters. Unavailable stage values are blank. Camera receipt
timestamps are host timestamps, not sensor exposure. Camera buffering, USB transit,
protocol transit and display latency are unavailable.

## Windows build and testing

From a PowerShell prompt in the repository after configuring the existing Windows
dependencies (see `tracker-neuralnet/BUILD.md`):

```powershell
.\tools\dev-command.bat cmake --build build --target install --parallel 6
.\tools\dev-command.bat ctest --test-dir build --output-on-failure --output-junit experimental-tests.xml
.\tools\verify-package.ps1 -Stage stage
$env:PATH="$PWD\stage;$PWD\stage\modules;"+$env:PATH
.\build\enhanced\tests\enhanced-inference-profile-tests.exe "$PWD\stage" > build\inference-profile.txt
.\build\enhanced\tests\enhanced-optical-flow-tests.exe benchmark > build\optical-comparison.txt
```

The installed-plugin tests use isolated temporary profiles, real ONNX sessions,
and a synthetic blank 640×480 75 FPS delivery request. They check capture lifecycle,
reconnection, clean missing-model/runtime errors, Qt controls and CSV export; they
do not establish physical PS3 Eye delivery or face-tracking accuracy.

For physical testing, extract the ZIP to a new directory, launch `opentrack.exe`,
select NeuralNet / PS3 Eye / 640×480 / 75 FPS and the existing protocol, then start
with experiments off. Record the same turns, stops, reversals, stationary intervals,
occlusion, disconnect/reconnect and recenter sequences in each of the four modes.
Use the Analyzer to record/export; compare validity, recovery, processing P50/P95/P99,
frame intervals and visual overshoot. Confirm mapping curves and Amplification/Speed
on the existing Mapping page. Reset Experimental Features restores experimental
defaults without changing the NeuralNet core, manual prediction, filter or mappings.

## Profiling method and limits

The opt-in inference executable benchmarks five actual shipped ONNX models at
1/2/4 CPU threads and disabled/basic/extended/all graph optimizations, using the
same fixed grayscale input and a forced face ROI. It records 40 measured executions
after three warmups, total adapter time, Run time, preprocessing/decoding remainder,
and process working set. It verifies fixed-shape model image-buffer addresses remain
stable, and measures frame-copy, resize and brightness normalization costs. This
does not count all ONNX/OpenCV internal allocations. The existing small 0.5 model,
two threads and default full graph optimizations remain unchanged.

[ONNX Runtime threading](https://onnxruntime.ai/docs/performance/tune-performance/threading.html)
and [graph optimizations](https://onnxruntime.ai/docs/performance/model-optimizations/graph-optimizations.html)
describe the controls used by the profiling executable. Available providers are
reported; this build deliberately uses CPU only. No unvalidated GPU option is added.

The four-mode comparison is a synthetic pose-stage benchmark on identical texture
and timestamped pose sequences, with ONNX inference, image generation and sensor
delivery excluded from the timed stage. It tests validity/outlier behavior and
flow overhead; it does not measure human tracking accuracy or prove added visual
smoothness. Detailed measured results and build/test totals follow below.

## Measured results — 11 October 2026

Local Windows x64 Release build, Intel Core i5-12600KF (10 cores / 16 logical
processors), ONNX Runtime 1.23.2, OpenCV 4.12, Qt 6.10.3. These are software
measurements on this host, not physical PS3 Eye, RTX 3070 or camera-to-display tests.
Available ONNX providers were CPU and Azure; all executions below used CPU.

Identical synthetic 640×480 textures and 1024 timestamped yaw observations at
13.333 ms intervals, including five isolated 100-degree spikes; 24 warmup
observations excluded from timing. OpenCV uses one thread in this benchmark.
All modes use the same existing predictor; only Dynamic mode changes its horizon.

| Pose-stage mode | P50 ms | P95 ms | P99 ms | Accepted observations | Held outputs | Rejected |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Standard NeuralNet processing (core off) | 0.0009 | 0.0025 | 0.0038 | 1024 | 0 | 0 |
| NeuralNet 2.0 Core | 0.0023 | 0.0052 | 0.0095 | 1019 | 5 | 5 |
| Core + Experimental Flow | 0.5292 | 1.5083 | 2.1032 | 1019 | 5 | 5 |
| Core + Dynamic Prediction | 0.0037 | 0.0068 | 0.0089 | 1019 | 5 | 5 |

All 1024 outputs were available in these sequences; five core outputs were
explicitly held, not newly validated measurements. Flow supplied 960 valid image
motion estimates but **zero applicable correction** at this synchronous cadence.
Its measured median overhead was about 0.527 ms relative to Core. This sequence
does not establish flow benefit. Dynamic prediction preserved validation decisions;
the deterministic tests separately prove stop/reversal damping, reliability scaling,
zero fallback, horizon caps and absence of accumulated polling offsets. Sub-microsecond
differences in the core benchmark should not be treated as reliable speed improvements.

The separate inference sweep ran 60 configurations, each with three warmups and
40 measured executions, using the actual shipped model adapter and a fixed
320×240 grayscale input (forced ROI for pose models). Model setup, full-resolution
pyramid construction, tracking/filtering, capture and rendering are excluded.
P99 over 40 executions is an empirical upper-tail reading, not a long-run guarantee.

| Model | Two threads, full optimization: P50 / P95 / P99 ms | Four threads, full optimization: P50 / P95 / P99 ms |
| --- | --- | --- |
| Localizer | 0.6819 / 1.0280 / 1.2382 | 0.6259 / 0.8261 / 1.0582 |
| Pose 0.2 small | 4.1014 / 5.6451 / 5.7178 | 2.7940 / 3.4727 / 3.4757 |
| Pose 0.2 big | 12.6969 / 15.3246 / 15.3964 | 6.8657 / 8.4921 / 8.5728 |
| Pose 0.5 small (existing default) | 3.8756 / 4.9326 / 5.2771 | 2.2892 / 2.8335 / 2.9111 |
| Pose 0.5 big | 12.2995 / 15.6013 / 15.6927 | 6.8909 / 8.4001 / 8.8361 |

Full optimization reduced the two-thread 0.5-small P95 from 8.7830 ms with graph
optimization disabled to 4.9326 ms. Four CPU threads were faster on this host in
the isolated trial; two threads remain the profile default because sustained
capture/inference concurrency and real-camera quality were not evaluated.
The big models at two threads exceeded the 13.333 ms frame budget at P95 even
before capture/pyramid overhead. Model quality cannot be inferred from these timings.

The sweep exposed the 0.2 models' symbolic quaternion batch dimension with graph
optimization disabled. The adapter now resolves only that symbolic batch to the
fixed input batch of one; non-batch dimensions and float type remain strictly
validated. All five models subsequently executed successfully in all graph/thread
configurations. A separate CI smoke test checks disabled/full optimization.

Fixed-shape adapter image-buffer addresses stayed stable after warmup in all 60
configurations. Working-set increase between warmup and completion was 0–52 KiB
per configuration; this is not an exhaustive allocation/leak audit. The reusable
buffer probe observed zero pointer changes. Full 640×480 grayscale copy P50 was
0.0158 ms, direct 288×224 resize P50 0.7012 ms, and brightness normalization P50
0.0537 ms. The existing image pyramid and reusable tensors remain in use; removing
the camera ownership copy would require a separately verified lifetime contract.

Raw reproducible local output is in `build/inference-profile.txt` and
`build/optical-comparison.txt`. Hardware quality, live-face fusion benefit,
sustained physical 75 FPS delivery, and display latency remain unverified.

## Changed source areas

- `tracker-neuralnet/optical-flow.hpp/.cpp`: bounded facial sparse LK.
- `tracker-neuralnet/experimental.cpp`: capture integration, Qt graphs and recording export.
- `tracker-neuralnet/ftnoir_tracker_neuralnet.h/.cpp` and `enhanced-controls.cpp`: anchors,
  independent controls, reset, loss/recenter lifecycle and diagnostic flow fields.
- `tracker-neuralnet/model_adapters.h/.cpp`: constrained symbolic-batch compatibility
  and read-only buffer probes; `CMakeLists.txt` links OpenCV video.
- `enhanced/flow.hpp`, `analyzer.hpp`, `experimental-options.hpp`, `motion.hpp`,
  `options.hpp`, `telemetry.hpp`, `api/plugin-api.hpp`, `logic/pipeline.cpp`:
  single-stage prediction/fusion, bounded telemetry and output feedback.
- `enhanced/tests/experimental.cpp`, `optical-flow.cpp`, `inference-profile.cpp`,
  `integration.cpp`, `pipeline-controls.cpp`, `settings.cpp`, `CMakeLists.txt`: tests/profile.
- `tools/verify-package.ps1`, `cmake/opentrack-install.cmake`, NeuralNet translation
  catalogs and documentation: runtime checks and installed documentation.

The existing Windows GitHub Actions workflow still builds/tests/validates the stage,
excludes test executables/previews from the ZIP, and uploads the Windows artifact.
Package verification now additionally requires mapping/application libraries,
the experiment documentation, and the OpenCV world DLL or core/imgproc/video DLLs.

## Completion validation

Windows x64 Release build and installation succeeded. All **99 CTest cases passed**
in 46.73 seconds, including the five-model compatibility smoke test, all three
flow quality modes, drift/occlusion/background rejection, resets, prediction,
actual nonlinear/asymmetric mappings and FreeTrack packet output, installed-plugin
capture/reconnect/loss, missing models/runtime, settings and recording export/reset.
The dependency-free build independently passed its **75-test subset** in 2.92 seconds.
The standalone inference sweep completed all 60 configurations with no buffer-address
changes. Qt offscreen previews were inspected for the experimental controls and analyzer.

The package verifier passed all 29 named critical files plus OpenCV runtime checks.
The Windows ZIP contains the rebuilt NeuralNet and adaptive plugins, PS3 Eye backend,
ONNX Runtime, five models, mapping/application libraries and Qt runtimes/plugins.
Use the complete package: tracking metadata and output-feedback interfaces have
changed, so separately compiled older binary plugins need rebuilding with these headers.
No source commit or push was made.

The downloadable local artifact is `opentrack-enhanced-windows-x64.zip`. CI continues
to upload the same artifact name when the changes are built there. Full logs are
`build/experimental-final-tests.log`, `build/experimental-final-core-tests.log`,
and `build/experimental-tests.xml`. No GitHub-hosted CI run or physical-camera
test was performed in this local session.

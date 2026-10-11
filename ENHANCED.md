# OpenTrack Enhanced

OpenTrack's tracker, filter, protocol, response-curve and profile architecture is retained.
The [completion report](ENHANCED-COMPLETION.md) records the executed build and tests.
New capture, adaptive-filter, prediction and head-control behavior is disabled by default. Enable one
feature at a time when comparing it with your existing profile.

## Implemented software

- **Head Movement Controls** add a master toggle, Amplification and Speed 0–100% sliders,
  live percentages, neutral Reset, axis overrides and response readouts in **Mapping**.
  Original/adjusted curve overlays preserve editable points and display output saturation.
  42% amplification is 1x gain; 55% Speed preserves the selected response. Gain runs once
  after mapping; timestamp-aware Speed converges to the mapped target using the original
  filter as a parallel reference. Adaptive temporal smoothing is not stacked with it.
  [HEAD-MOVEMENT-CONTROLS.md](HEAD-MOVEMENT-CONTROLS.md) documents formulas, the guarded
  target interface, 120 ms transitions, response estimates and hardware checks.

- NeuralNet uses monotonic, submillisecond host timestamps. Pose and acquisition timestamp
  are published together. Invalid tracking produces NaN values and resets prediction.
- Optional capture worker: one pending frame, latest first, reusable three-buffer ownership
  handoff, condition-variable waits, bounded polling and joined shutdown. Frames older than
  the configured 5–500 ms host age are discarded before inference or pose publication.
  Capture mode changes require restarting tracking.
- Measured capture-delivery rate, inference-pipeline rate, inference duration, host frame age,
  host processing latency, rolling P50/P95/P99, interval mean/P95, queue replacements,
  stale frames, backend sequence gaps, tracking validity and successful reconnections.
  Diagnostics has CSV snapshot export and a plugin API snapshot for automated comparisons.
  Full application processing latency measures host receipt to the first completed protocol
  submission for each valid sample, with separate P50/P95/P99; held/invalid/duplicate samples
  are excluded. Protocol transport and display latency remain unmeasured.
- Selectable **Enhanced Adaptive Filter**: independent rotation/translation smoothing,
  speed-adaptive response, stationary jitter protection, frame-interval compensation,
  angular wraparound, implausible-spike rejection, bounded recovery after tracking loss,
  optional confidence attenuation, recenter reset and optional yaw correction display.
  Existing filters remain available. Select the new filter AND enable its checkbox.
- Optional prediction runs once after the selected filter and before response curves/output
  mapping. It estimates angular/translation velocity using acquisition intervals, attenuates
  inconsistent/noisy motion, suppresses reversal/loss prediction, limits extrapolation and
  resets on recenter. Horizon is 0–15 ms; defaults are 8 ms, 3 degrees and 0.5 cm limits.
  Repeated output polls do not accumulate extrapolation.
- PS3 Eye detection checks USB VID/PID 1415:2000 dynamically. Mode enumeration uses the
  installed open driver's rate-normalization function, excluding corrupt high-speed modes.
  Exposure, gain and automatic exposure/gain use the existing driver settings dialog.
  Shared frame copies are protected by a process-shared atomic lock; only active pixel
  bytes are copied. Startup clears old state. Failure detection and optional bounded
  reconnects use increasing delays (250, 500, 1000, 2000, then 4000 ms; at most 10 retries).
  Automatic retry failures do not open modal dialogs or switch to another configured camera.
- Windows DirectShow cameras enumerate advertised format/FPS tuples and USB identity
  when exposed by the driver. Their existing native property pages provide device controls.
  Unsupported buffer-size requests are best effort; no unsupported modes are synthesized.
- The existing Qt tracker settings contain Enhanced tabs: Performance, Adaptive Smoothness,
  PS3 Eye Camera, Prediction and Diagnostics. Mapping owns Head Movement Controls.
  Settings use additive per-profile INI keys.
  Save/Cancel follows the existing options bundles; old profiles retain disabled defaults.
- Windows packaging puts ONNX Runtime in **modules/onnxruntime.dll**, includes all five
  models, Visual C++ runtime DLLs and the optional provider helper. Missing/incompatible runtime initialization now
  returns a tracker error instead of aborting the process. CI requires NeuralNet dependencies.

## Partial support and limitations

- Physical PS3 Eye validation has **not** been performed. USB transport, actual exposure,
  sensor frame loss and camera-to-display latency cannot be inferred from host timing.
  Capture FPS means frames delivered to the application; internal camera/driver drops are
  unknown. PS3 sequence gaps count missed subprocess frames, not all sensor/USB losses.
- In this open driver, VGA 60/75 FPS are enumerated; exact QVGA 120 FPS is **not** a supported
  table entry. A legacy request of 120 negotiates 100 FPS and is logged accordingly. QVGA
  125 is enumerated. DirectShow 120 FPS is listed only if its installed driver advertises it.
  No USB drivers are installed or replaced by this project.
- Prediction/filter confidence handling is implemented for optional tracker-supplied values.
  NeuralNet supplies timestamps and validity, but no calibrated confidence value. The existing
  model covariance filter remains available; covariance is not presented as face confidence.
- DirectShow enumeration currently lists exact advertised format intervals, not every possible
  rate in a continuous min/max interval. Arbitrary camera controls remain backend dependent.
- Reconnection depends on the backend returning from capture. PS3 polling times out after
  about two seconds. OpenCV/DirectShow can block longer inside a defective driver.
- No acceleration prediction is added: velocity, noise attenuation and explicit caps are the
  conservative implementation. No claim of reduced true display latency is made.
- Plugin virtual interfaces and the PS3 subprocess shared-memory layout gained additive
  fields/methods. Deploy this build's modules and subprocess together; do not mix old binaries.
  Existing INI profile keys and module selection names remain compatible.
- The source workspace initially had no `.git` metadata. No commit or remote CI run is
  claimed. Earlier Enhanced work extended the original Windows workflow while retaining
  its dependency/module setup; this Mapping correction leaves that working workflow intact.
  Optional SDK modules still depend on their SDK availability.

## Local Windows build

Use VS 2022/2026 x64 C++ tools, CMake and Ninja, Qt 6 with Widgets/LinguistTools/SerialPort,
OpenCV, CPU ONNX Runtime and the existing PS3 Eye/libusb dependency. The launcher discovers
Visual Studio using `vswhere`, instead of assuming an Enterprise installation path.

From the repository root, after placing SDKs at the paths shown:

```bat
tools\dev-command.bat cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=D:/Projects/OpenTrack-Enhanced/stage -DQt6_DIR=D:/Projects/OpenTrack-Enhanced/.deps/qt/6.10.3/msvc2022_64/lib/cmake/Qt6 -DOpenCV_DIR=D:/Projects/OpenTrack-Enhanced/.deps/opencv/opencv/build/x64/vc16/lib -DONNXRuntime_DIR=D:/Projects/OpenTrack-Enhanced/.deps/onnxruntime -DSDK_LIBUSB=D:/Projects/OpenTrack-Enhanced/.deps/opentrack-depends/libusb-msvc-2026-md -DOPENTRACK_REQUIRE_NEURALNET=ON -DBUILD_TESTING=ON
tools\dev-command.bat cmake --build build --target install --parallel 8
tools\dev-command.bat ctest --test-dir build -C Release --output-on-failure --output-junit enhanced-tests.xml
powershell -NoProfile -File tools\verify-package.ps1 -Stage stage
```

SDK-independent math/concurrency tests:

```bat
tools\dev-command.bat cmake -S . -B build-enhanced -G Ninja -DOPENTRACK_ENHANCED_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
tools\dev-command.bat cmake --build build-enhanced --parallel 4
ctest --test-dir build-enhanced --output-on-failure
```

Integration tests load the installed NeuralNet DLL and real models using a synthetic camera.
Both serial and latest-frame paths exercise one injected disconnection, automatic recovery,
invalid-pose delivery and shutdown. The test also renders the Qt settings dialog. It does not
claim human-face accuracy or hardware validation. Run install before the integration tests.
An isolated child-process test verifies that a missing runtime returns an initialization
error and destroys the tracker cleanly without accessing an uninitialized ONNX API.
Mapping tests run the real pipeline with adaptive and legacy filters, render the original
six-axis editor and advanced controls, and exercise save/cancel. A separate isolated test
reads the unchanged production FreeTrack packet writer's mapped output.

## Exact GitHub Actions sequence

Open **Actions → OpenTrack Windows x64 Full Build → Run workflow**. The workflow remains
`.github/workflows/cmake.yml`, with `push`/`pull_request` on `master` and manual dispatch.

1. Checkout OpenTrack with recursive submodules.
2. Checkout `opentrack/opentrack-depends` with recursive submodules.
3. Set up Ninja.
4. Install OpenCV using the existing action, including camera/calibration modules.
5. Install Qt 6.10.3 with SerialPort, Multimedia and ImageFormats.
6. Download ONNX Runtime 1.23.2 NuGet package; require headers, import library and x64 DLL.
7. Configure Ninja Release with Qt/ONNX paths, `OPENTRACK_REQUIRE_NEURALNET=ON`,
   `BUILD_TESTING=ON`, and the workspace `stage` install prefix.
8. Verify ONNX entries in `CMakeCache.txt`.
9. Build and install, parallelism 4, through the Visual Studio launcher.
10. Require `opentrack.exe`, NeuralNet DLL, **modules/onnxruntime.dll** and five model files.
11. Run CTest with failures shown and JUnit output.
12. Run `tools/verify-package.ps1` to require the camera/filter/Qt/runtime files.
13. Upload the JUnit result even on test failure.
14. List installed DLL plugins.
15. ZIP `stage` as `opentrack-enhanced-windows-x64.zip`.
16. Upload artifact `opentrack-enhanced-windows-x64`, with missing-file failure and 30-day retention.

## Hardware acceptance procedure

1. Duplicate your working profile. Keep the original as the baseline; start NeuralNet with
   all Enhanced behavior disabled. Verify the camera name, resolution, model and output.
2. Select a mode advertised by your backend. Test VGA 640×480/60, then 75 if enumerated.
   Test QVGA 320×240/120 only for a backend that actually advertises it. Check negotiated
   mode logs and measured capture delivery rate; do not infer support from the request box.
3. Enable diagnostics, allow at least 256 frames, export a CSV snapshot. Enable latest-frame
   capture, restart, repeat the same head motions and export a second snapshot. Compare host
   latency quantiles, validity and queue/stale counts while keeping the model identical.
4. Select Enhanced Adaptive Filter, disable NeuralNet's internal filter for the comparison,
   enable adaptive smoothing. Test stationary jitter, rapid turns, ±180-degree wraparound,
   translation, recenter and covering/uncovering your face. Confirm bounded reacquisition.
5. Enable prediction at 5–8 ms. Verify rapid turns, stops, reversals and the displayed angular/
   translation offsets. Increase toward 15 ms only after checking overshoot in your game.
6. Enable reconnect, unplug/replug the camera, confirm validity becomes unavailable, retries
   remain bounded, recovery uses the same camera, and stopping during retry completes.
7. Adjust exposure/gain and automatic mode in the backend dialog. Save/restart the profile,
   then check cancellation and restored values. Observe lighting-related FPS changes.
8. For physical latency claims, use an external high-speed camera/LED or equivalent measured
   rig covering motion, camera acquisition and display. Host diagnostics alone are insufficient.

## Source changes

- `enhanced/{telemetry,latest-frame,reconnect,motion,ipc-lock,options,widgets}.hpp`
- `enhanced/tests/{CMakeLists.txt,core.cpp,settings.cpp,integration.cpp,head-controls.cpp,pipeline-controls.cpp,protocol-packet.cpp,isolated-freetrack.hpp}`
- Head controls additionally change `enhanced/{head-controls.hpp,head-controls-ui.hpp}`, shared
  guarded-target API, pipeline diagnostics, `gui/mapping-dialog.{hpp,cpp}`,
  `spline/spline-widget.{hpp,cpp}`, `gui/options-dialog.{hpp,cpp}` and
  `opentrack/main-window.cpp`; their behavior is documented in `HEAD-MOVEMENT-CONTROLS.md`.
- `filter-enhanced-adaptive/{CMakeLists.txt,filter.hpp,filter.cpp}`
- `api/plugin-api.hpp`, `logic/pipeline.hpp`, `logic/pipeline.cpp`
- `tracker-neuralnet/{ftnoir_tracker_neuralnet.h,ftnoir_tracker_neuralnet.cpp,enhanced-controls.cpp,model_adapters.h,model_adapters.cpp,env.cpp,CMakeLists.txt}`
- `video/{camera.hpp,camera.cpp}`
- `video-opencv/{impl.hpp,impl-camera.cpp,capabilities.cpp}`
- `video-ps3eye/{module.hpp,module.cpp,wrapper.cxx,shm-layout.hpp,CMakeLists.txt}`
- `CMakeLists.txt`, `cmake/{FindONNXRuntime.cmake,opentrack-install.cmake,opentrack-version.cmake}`
- Earlier Enhanced implementation: `.github/workflows/{cmake.yml,build-windows.bat}`,
  `tools/{dev-command.bat,verify-package.ps1}`; unchanged by the Mapping correction.
- `opentrack/main-window.cpp`, `.gitignore`, `README.md`, `ENHANCED.md`
- Restored the missing `video-ps3eye/ps3eye-driver` submodule checkout without changing its source.

Generated builds, downloaded SDKs, test results, UI previews and package ZIPs are local artifacts.
Build verification may refresh existing translation catalogs through OpenTrack's normal lupdate target.

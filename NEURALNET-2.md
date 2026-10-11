# NeuralNet 2.0

NeuralNet 2.0 is optional and **disabled by default**. Enable it in NeuralNet settings →
Enhanced → NeuralNet 2.0. Existing profiles retain their original tracker processing.
No model, capture resolution, camera FPS selection, mapping curve, USB driver or firmware
is replaced. Settings use additive `nn2-*` keys in the existing `neuralnet-tracker` bundle.
Save and Cancel use that bundle's existing persistence behavior.

## Actual processing order

1. Camera host receipt timestamp → existing serial capture or single pending latest-frame slot.
2. Age gate → existing image pyramid, face localizer and ONNX pose estimator.
3. World-coordinate conversion → **raw temporal validation**, in cm and degrees using the
   existing axis signs. Corrupt model outputs are distinguished from absent faces.
4. Existing optional model covariance filter, **only on accepted measurements**.
5. **Time-based recovery** and bounded shortest-arc quaternion/translation convergence.
6. Atomic pose + acquisition timestamp + hold/recovery/motion metadata publication.
7. Existing camera offset and centering → selected filter. The enabled Enhanced Adaptive
   Filter consumes the motion metadata in its existing smoothing stage. It skips duplicate
   outlier/recovery guards when the corresponding NeuralNet 2.0 stage owns them.
8. Existing 0–15 ms prediction → original mapping curves → existing Amplification/Speed
   response → FreeTrack/TrackIR or another selected protocol.

There is no additional tracker smoothing stage or inference queue. Motion-Aware
Stabilization requires **Enhanced Adaptive Filter selected and enabled**. Other filters
continue to work; diagnostics distinguishes requested stabilization from active adaptive
processing. NeuralNet's covariance filter remains independently selectable, as before.
Mapping Speed still owns temporal response when using the guarded mapping target.

## Validation and movement

All six values must be finite. Conservative sanity bounds reject translations beyond
1,000 cm and malformed rotations beyond 3,600 degrees. These are corruption bounds,
not limits on ordinary movement. Angular differences use the shortest wrapped delta.
Velocity thresholds use actual acquisition intervals, with a configurable angular limit
(default 1,080 degrees/s), translation limit (250 cm/s), and strength-dependent noise slack.
An allocation-free five-sample timestamped history supplies median velocity for stationary
residual protection after at least 35 ms of evidence. Classification identifies stationary,
slow, rapid, stopping and reversing motion.
For small local fluctuations, a fixed-storage Theil-Sen slope separates alternating noise
from consistent slow drift. Larger changes and stops use immediate velocity. This history
classifies motion and does not buffer or smooth the published tracker pose.

A suspicious jump is rejected immediately. A second continuous observation within 80 ms
can confirm a changed position, allowing persistent rapid movement without permanent
clamping. Ordinary accepted samples are published without waiting for a future sample.
Rejected raw values never replace the validated timestamp. Optional hold behavior is
explicitly marked; it is not a newly accepted pose.

The adaptive filter consumes motion state to protect stationary jitter, increase rapid
response and clear velocity memory at stops/reversals. It maintains its original path
when NeuralNet 2.0 metadata is absent. Estimated reliability is separate from the API's
optional model `confidence`; NeuralNet does not supply calibrated confidence.

## Recovery states and timestamps

| State | Behavior |
| --- | --- |
| TRACKING | Accepted pose available; prediction eligible. Startup accepts the first sane pose. |
| SUSPECT | Short failed observation. Hold, if enabled, uses the last output and original timestamp. Prediction is reset. |
| LOST | Hold expired or loss timeout elapsed; tracker publishes NaN on all axes. No extrapolation. |
| REACQUIRING | Continuous observations confirmed for 60 ms, then bounded convergence. Prediction stays disabled until TRACKING. |

Default hold is 50 ms, measured both since the first failure and since the last good
publication. Default SUSPECT → LOST timeout is 100 ms. Polling expires holds even without
another frame, and long silent capture gaps enter recovery. Camera disconnection goes
directly to LOST with no hold and remains a separate `connected` diagnostic. Inference
started before a disconnect/reconnect or recenter cannot publish as a new current pose.

Recovery uses rigid-orientation quaternion interpolation in NeuralNet's existing
`Ry(-yaw) Rz(pitch) Rx(-roll)` convention, including wraparound and large rotations.
Translation is linear in the existing coordinate system. Default blend duration is
120 ms; steps are capped at 720 degrees/s and 100 cm/s using actual reacquisition intervals.
Responsiveness scales confirmation/blend timing and these speed bounds. Convergence may
take longer for large changes. Confirmation publishes invalid state, followed by valid
bounded recovery outputs. Missing/corrupt recovery observations return to LOST.

Recenter clears validation, recovery and velocity history. The current validated raw
observation remains available for the application's existing centering transform, so its request
does not loop forever. Counters are retained until the tracker session ends. Enable/disable
transitions reset temporal state; the disabled output continues through the legacy path.
During recovery this uses the observed head pose, rather than an intermediate blend,
so subsequent steady observations do not jump away from the new center. Without a fresh
validated observation the center request waits for tracking to return.

The existing protocol error path retains its last safe mapped output when the tracker
publishes NaN. This protocol compatibility behavior does not make tracking valid or
advance prediction. There is no new protocol validity bit in FreeTrack/TrackIR.

## Reliability and diagnostics

Estimated tracking reliability is a normalized heuristic derived from accepted temporal
continuity, valid-observation duration, frame freshness and tracking state. It decays with
elapsed loss time, independent of output polling frequency. It is **not a NeuralNet
probability or calibrated confidence**. Protection uses positive stationary-history
evidence; a low score cannot lock out reacquisition.

Live diagnostics and CSV snapshot export include:

- Current enabled/state/reliability/motion/raw-valid/output-valid/hold/recovery readings.
- Session totals for rejected poses, detected spikes and reacquisition **attempts**.
- Current loss duration, raw and last validated acquisition timestamps, host capture-to-pose
  age, validation/recovery execution duration excluding the covariance-filter callback.
- Stabilization requested/active, alongside existing capture/inference rates and host latency
  P50/P95/P99, prediction, mapping and protocol submission metrics.

Timestamps are monotonic host times, not wall-clock timestamps. Host receipt is the earliest
available time: sensor exposure, driver buffering, USB transit and display latency are
unavailable. Latest-frame mode keeps its configured 5–500 ms age limit. Serial NeuralNet
2.0 uses a separate default 150 ms maximum pose age. CSV export is a snapshot, not a full
motion recording. Percentile sorting remains on the diagnostics consumer; pose history
and recovery use fixed storage in the inference path.

## Reproducible software verification

```powershell
.\tools\dev-command.bat cmake --build build --target install --parallel 6
.\tools\dev-command.bat ctest --test-dir build --output-on-failure --output-junit neuralnet2-tests.xml
.\build\enhanced\tests\neuralnet2-tests.exe benchmark
powershell -NoProfile -File tools\verify-package.ps1 -Stage stage
```

Deterministic timestamped tests cover steady/fast/irregular motion, stops, reversals,
wraparound, translation spikes, NaN/Inf, persistent movement, intermittent/long loss,
silent gaps, confirmation/convergence, disconnect, recenter, rapid toggles and options.
Installed-plugin tests exercise real ONNX sessions with identical blank 640×480 synthetic
frames delivered on a requested 75 FPS schedule,
serial/latest capture, reconnection, no-face publication, settings/UI and missing runtime.
Application-pipeline tests exercise NeuralNet 2.0 metadata with the actual adaptive plugin,
prediction, nonlinear/asymmetric curves, Amplification/Speed, protocol output and recenter.

The benchmark replays the same 30,000 **synthetic timestamped poses**, including 30 injected
angular outliers, with the core ON and OFF. It reports mean/P50/P95/P99 processing time,
rejected samples, accepted-pose changes, fixed pipeline storage and process working set.
It is not a captured-face quality test or an end-to-end camera/inference benchmark.
Hardware acceptance requires identical recorded PS3 Eye inputs and actual 640×480/75 FPS
capture measurements. Physical camera FPS, quality improvements and camera-to-display
latency have not been validated here. The original bounded capture worker and scheduling
are retained; no speculative scheduler or ONNX execution changes are made.

## Executed local results — 11 October 2026

- Windows x64 Release application and installed modules built successfully with MSVC/Qt 6.
- Full CTest suite: **78/78 passed**, including installed ONNX/camera/UI/CSV integration,
  actual adaptive/prediction/mapping/protocol paths and all existing regression tests.
- Package verifier: **24 required runtime/document files present**, including
  `modules/onnxruntime.dll` and all five models. GitHub Actions workflow remains unchanged;
  no remote CI run or physical-camera acceptance is claimed. Changes were not committed.

One 30,000-pose deterministic replay on this host:

| Core | Mean ms | P50 ms | P95 ms | P99 ms | Rejected injected outliers |
| --- | ---: | ---: | ---: | ---: | ---: |
| OFF | 0.00006159 | 0.0001 | 0.0001 | 0.0001 | 0/30 |
| ON | 0.00063869 | 0.0005 | 0.0009 | 0.0011 | 30/30 |

Accepted poses were unchanged by the validation/recovery core during normal tracking;
no future-sample buffering delay was introduced. Stationary-jitter and bounded-recovery
behavior are verified separately with synthetic tests. The pipeline object is 1,136 bytes
of fixed storage. Process working set before/after the replay harness was 12,689,408 /
13,062,144 bytes; that includes the harness's input/timing arrays, not just pipeline state.
Submicrosecond timings are resolution-sensitive and are not end-to-end latency.

One short run per mode with the same blank synthetic 640×480 frames and requested 75 FPS:

| Mode | Capture FPS | Inference-pipeline FPS | Host processing P50/P95/P99 ms |
| --- | ---: | ---: | --- |
| Serial OFF | 63.33 | 63.32 | 1.205 / 1.742 / 1.908 |
| Serial ON | 64.38 | 64.39 | 1.165 / 1.678 / 1.769 |
| Latest OFF | 69.63 | 69.63 | 1.193 / 1.785 / 2.177 |
| Latest ON | 69.89 | 69.91 | 1.239 / 1.815 / 2.675 |

These are measured mock-backend delivery rates, not 75 FPS hardware acceptance. Its timed
sleep and serial inference add host scheduling overhead. Blank frames exercise the face
localizer and loss path; they do not measure head-pose inference with a visible face.
Short unpaired runs and host load do not establish a performance or quality improvement.
Raw results are saved in `build/nn2-benchmark.txt`, `build/nn2-test.log`,
`build/neuralnet2-tests.xml` and `build/Testing/Temporary/LastTest.log`.

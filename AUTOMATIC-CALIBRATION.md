# OpenTrack Enhanced 2.1 — automatic head movement calibration

Open **Mapping → Automatic calibration…** while tracking with **NeuralNet 2.0** and
**pose validation** enabled. Calibration is optional; loading an old INI does not
enable it or change any mapping points.

## Using the wizard

1. Sit normally and look straight ahead. Choose a use case and optionally include
   roll and X/Y/Z translations. Press **Record**; the existing Center operation
   establishes neutral. Allow it to settle, then hold neutral for two seconds.
2. Record at least four seconds of relaxed stationary tracking. Deliberate motion,
   large drift, or a large noise envelope requires a retry instead of a larger deadzone.
3. Move gently left, right, up, and down. **Move first, then press Record** at each
   comfortable endpoint and hold it for at least two seconds. Confirm physical
   comfort separately for every measurement. Never force your neck.
4. Complete the optional roll and translation steps if selected. Otherwise those
   mapping axes keep their current curves. Each calibrated axis needs both signed
   branches; repeating the same direction is rejected.
5. Review the measured physical ranges, noise, quality and actual spline preview.
   Edit each virtual target, neutral deadzone, and response Shape (1.2–3). Yaw defaults
   to **150° virtual camera movement in each direction**, not 150° physical rotation.
   The physical range columns identify which branch represents left/right or up/down.
6. Confirm replacement and press **Apply curves and stop tracking**. Tracking stops
   through the ordinary application lifecycle before points and limits are replaced.
   The profile is saved. Restart tracking when ready. Cancel before applying discards
   the draft. **Undo calibration / previous mapping** restores and saves the prior affected
   points, alternate-branch flags, limits, and calibration record after confirmation.

Enable mapping curves and centering, and disable automatic centering before starting.
Amplification **42%** is neutral (1×). The wizard compensates the current gain once
in generated points; gain and Speed controls are preserved. Unreachable targets at
the native output limit require lower targets or a manually chosen gain. Changing
Amplification later changes virtual movement as usual. For editable styles, racing
templates, axis selection and Save as, open [Smart Mapping](SMART-MAPPING.md) from review.
Calibrated axes must use their own tracker source. Translation calibration additionally
requires relative translation and dynamic neck displacement to be disabled, because
their game-space translation depends on mapped rotations. These conditions are checked;
the wizard does not silently change them.

## Measurement and curve behavior

- Input comes from the NeuralNet 2.0 **accepted physical observation**, before internal
  covariance filtering, recovery interpolation, optical-flow assistance, application
  filtering, prediction, mapping, Amplification, Speed, or protocol submission.
  The existing source selection, camera correction, centering, and input inversion
  transform this observation into the coordinates consumed by Mapping.
- A fixed 1024-slot single-producer/single-consumer queue is active only for calibration.
  Its producer allocates nothing, takes no additional locks, and copies only six pose
  coordinates and metadata. Sorting, statistics, curve generation, previews and INI
  persistence run on the UI thread. No camera frames or extra inference are introduced.
- Each segment needs **60 unique valid samples and two seconds** (four seconds for
  stationary noise). Held/recovering, stale (>250 ms), nonfinite, implausible, or
  backward-timestamp observations are rejected. Timestamp gaps, loss and queue overflow
  require a segment retry. Confidence and estimated reliability, when supplied, must
  be finite and at least 0.65. Overall segment quality must also meet that threshold.
- Duplicate pipeline polls do not count as new samples. Sudden high-speed pose jumps
  require a retry. A robust median (P50) estimates each held endpoint; P05/P95 spread
  and sign consistency reject unsteady holds. Short-lived extrema never define range.
- Center epochs persist between steps and into review. Recenter or a new tracking
  session restarts the wizard from neutral; partial measurements are not combined.
- Stationary statistics include per-axis median, MAD, P95 absolute deviation, early-to-late
  median drift, interval P05/median/P95, effective valid rate, valid/rejected count,
  and segment reliability. Angular statistics handle wraparound. Drift and a large
  envelope are conservatively marked ambiguous, without diagnosing their source.
- Recommended deadzone is `1.2 * max(3 * MAD, P95 deviation)`, capped at 2°/0.5 cm and
  20% of the smaller measured range for the initial curve. Review permits an explicit
  deadzone below 40% of that range. Shape 2 softens small motion; reducing it toward
  1.2 increases small-motion response. Existing Enhanced Adaptive Filter knobs are
  recommended as manual starting points only; no filter or smoothing settings are applied.
- Main and alternate splines retain OpenTrack's positive-magnitude control-point
  convention: the main branch serves positive input, the alternate branch negative
  input. Unequal physical ranges and unequal virtual targets are supported.
  Generated power-shaped nodes use a double-width first interval to avoid an initial
  Catmull-Rom reversal at Shape 3. Both branches are checked with the **real spline
  evaluator** before application. Its existing bucket resolution and signed interpolation
  remain unchanged; endpoint checks account for that finite resolution.

## Profile storage and rollback

The additive `[automatic-calibration]` INI group contains a compact JSON `record` and
a JSON `rollback` snapshot and persistent `original` snapshot. Schema version 2 stores UTC calibration date, use case,
signed physical ranges, duration/count/quality/comfort confirmation, stationary and
timestamp statistics, deadzones, shaping, virtual targets, response style, sensitivities,
generation gain and generated/edited control points. Schema 1 loads with legacy defaults.
Unknown schemas and malformed records are rejected without changing curves. Profile
loading never reapplies the record; normal spline groups remain the authoritative maps.
The Mapping dialog owns save/cancel, including calibration's shared options bundle.

The snapshot contains exact previous main/alternate control points, alternate-enable
flags, input/output clamps and previous calibration record. Only affected axes restore.
One prior calibration application is retained, including across application restarts.
Apply/rollback validate the complete input before mutation and require explicit UI approval.

## Verification and limits

Build and focused tests:

```bat
tools\dev-command.bat cmake --build build --target install --parallel 8
tools\dev-command.bat ctest --test-dir build -R "^calibration-" --output-on-failure --output-junit calibration-tests.xml
tools\dev-command.bat ctest --test-dir build --output-on-failure --output-junit calibration-regression-tests.xml
powershell -NoProfile -File tools\verify-package.ps1 -Stage stage
```

Deterministic tests cover stable poses, stationary jitter, intentional drift, asymmetric
ranges, all six signed axes, accidental motion, sample/duration minima, duplicate polls,
NaN/Inf, loss, unreliable samples, timestamp gaps, recenter, deterministic monotonic
points, a response/range/deadzone grid evaluated by the real spline, continuity and sign,
queue overflow/concurrency, old INI compatibility, record persistence, exact rollback,
the real pipeline's observation stage, and the Qt wizard's preview/approval/optional axes,
loss and recenter paths. Qt render snapshots are written to `build/enhanced/tests`.

Calibration currently requires a tracker that supplies explicit validated physical-pose
metadata; NeuralNet 2.0 implements this. Legacy trackers and validation-disabled NeuralNet
are not treated as proven physical input. Incompatible pipeline options must be adjusted
manually. Filter recommendations are conservative starting values. Synthetic test inputs
prove software behavior; no physical PS3 Eye, human comfort, model accuracy, game response,
or camera-to-display latency validation is claimed. Perform the wizard and gameplay
checks with your camera and a copied working profile before adopting the curves.

### Initial calibration-engine results — 2026-10-11

- Windows x64 Release build and install: **passed**, using the existing MSVC/Qt 6
  build and runtime dependencies. The runnable build is `stage/opentrack.exe`.
- Complete regression suite: **121/121 passed**, 51.82 seconds. Evidence:
  `build/calibration-regression-tests.xml`.
- SDK-independent build and suite: **90/90 passed**, 2.81 seconds. Evidence:
  `build-enhanced/calibration-core-regression-tests.xml`.
- Initial calibration checks with the former neutral-gain guard and bulk-point update:
  **22/22 passed**. Existing adaptive/legacy filter pipelines, with and without
  NeuralNet 2.0: **4/4 passed**. Evidence: `build/calibration-final-tests.xml` and
  `build/calibration-final-pipeline-tests.xml`.
- Package verification: **30 required files passed**, including the updated app,
  NeuralNet, Enhanced Adaptive Filter, PS3 Eye runtime, ONNX models and this guide.
- Qt neutral/review views were rendered and visually inspected. The test fixture
  used known synthetic poses; these screenshots are software QA, not a real calibration.
- No commit or push was made. Physical camera/gameplay verification remains unperformed.

The later Smart Mapping revision replaces the neutral-gain guard with explicit gain
compensation and adds schema 2, selected-axis persistence and profile transport. See
[SMART-MAPPING.md](SMART-MAPPING.md) for the final expanded verification results.

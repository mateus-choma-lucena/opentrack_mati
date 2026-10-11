# Head Movement Controls in Mapping

Open **Mapping → Head Movement Controls**. This is the sole settings location for
Amplification and Speed; the existing six axis tabs and editable response points remain.
The master switch defaults to off. **42% Amplification = 1.0x; 55% Speed = the original
selected-filter response.** Enabling those neutral values preserves existing behavior.
These reference points use this fork's formulas, not a proprietary Beam algorithm.

## Amplification and the curve preview

Amplification scales the result of the existing mappings, once, in X/Y/Z/yaw/pitch/roll
order. Translations remain centimeters and rotations remain degrees. For percentage `p`:

```text
p <= 42: gain = 2^((p - 42) * 2 / 42)
p > 42:  gain = 2^((p - 42) * 2 / 58)
adjusted target = clamp(original mapped output * gain, -Max output, +Max output)
```

| Amplification | Gain | Original 20-degree mapped yaw |
|---|---:|---:|
| 0% | 0.25x | 5 degrees |
| 42% | 1x | 20 degrees |
| 100% | 4x | 80 degrees |

The optional six axis percentage fields **replace** global gain on their axis. They do
not multiply it. The global slider is disabled while overrides are selected.

When enabled, the original response is dashed and the adjusted steady-state target is
solid orange. Editable points still belong to the original curve. Main and asymmetric
curves use the same gain-and-limit function as the runtime; neither curve's points are
rescaled or overwritten. The orange plateau shows saturation. The green marker shows
the adjusted steady-state target for the current curve input, not the delayed protocol
pose. Speed changes timing, so it does not distort this static curve.

Gain follows both rotation/translation curves and the existing relative-translation/neck
transform. The existing fixed zero offset is added afterward and is not amplified.
Final axis inversion and protocol conversion follow the original rules. The displayed
movement limit excludes that offset; for example, yaw limited to 180 degrees plus a
3-degree offset can produce 183 degrees. If mapping curves are disabled globally, their
limits are also bypassed to preserve the existing unmapped mode.

## Speed after mapping

Speed controls how quickly output reaches the mapped target. Its percentage `s` maps to:

```text
s <= 55: multiplier = 2^((s - 55) * 3 / 55)
s > 55:  multiplier = 5^((s - 55) / 45)
```

The anchors are 0% = 0.125x, 55% = 1x and 100% = 5x. **55% bypasses the new response
stage exactly.** Other values run one response stage in mapped output coordinates:

```text
cutoff = clamp(observed baseline cutoff * multiplier, 0.05, 60) Hz
alpha = 1 - exp(-2*pi*cutoff*actual_output_dt)
output += alpha * (mapped_target - output)
```

Actual output intervals use the monotonic host clock, so output can continue converging
between camera frames. There is no new frame buffer, sleep or worker thread. Virtual
mapped rotations use linear differences; they do not wrap a virtual +170 to -170 turn
through a physical-head shortest-angle calculation. Each response step is a convex
interpolation, preventing response overshoot. Cached state is constrained when a limit
is reduced.

The original selected filter keeps running as a **parallel reference**, preserving its
state and settings. Both reference and target go through the actual original mappings,
including asymmetric curves and relative translation. On each fresh source sample, the
response observes the filter's mapped convergence fraction:

```text
fraction = (reference - previous_reference) / (target - previous_reference)
baseline cutoff = clamp(-log(1 - fraction) / (2*pi*sample_dt), 0.4, 60) Hz
```

Only finite useful exponential fractions update the estimate. Passthrough uses a 12 Hz
baseline. Non-exponential/undefined fractions retain the last useful estimate, initially
12 Hz. Thus unknown third-party filters are modeled rather than perfectly replicated
away from neutral; exact original behavior is guaranteed at neutral or disabled.
Higher nonneutral Speed values converge faster for the same reference and target until
the 60 Hz cap. An unsmoothed original passthrough already has no response lag, so no
setting can promise to improve its zero-lag neutral path.

Enhanced Adaptive Filter supplies its accepted, confidence/deadband/spike/recovery-guarded
measurement **before** temporal smoothing through `filter_sample_for_mapping()`. Speed
responds to that guarded target after mapping; the adaptive exponential result is only
the parallel reference. The two temporal responses are not stacked. Existing filters
use their original `filter_sample()` plus an unsmoothed target via the default interface.
NeuralNet's optional internal covariance filter remains explicitly configured by the
user; turn it off when comparing a single external smoothing response.

The live readout reports the selected axis's effective Hz and `1000/(2*pi*Hz)` millisecond
time constant. These are **model estimates**, not measured camera-to-display latency.
Before tracking starts, the label explicitly identifies the 12 Hz baseline estimate.

## Pipeline, transitions and profiles

```text
tracker pose + acquisition timestamp
-> original axis selection, camera offset and centering
-> original selected filter reference + guarded/unsmoothed target
-> bounded optional prediction (estimated once from the original filtered pose)
-> same prediction offset applied once to reference and target
-> existing rotation curves -> existing relative translation/neck -> translation curves
-> Amplification once per mapped path, with output limits
-> optional timestamp-aware response to the mapped target
-> original hold/zero, fixed offsets, final inversion
-> unchanged protocol pose/raw submission
```

Speed does not change capture FPS, ONNX inference, prediction horizon/caps, or saved
adaptive smoothing settings. Prediction never receives amplified or output-response data.
Raw protocol fields retain the original unamplified pose. Protocol implementations and
FreeTrack/TrackIR shared-memory layouts are unchanged.

Setting changes blend over a monotonic 120 ms cubic smoothstep. On/off handoffs begin at
the current reference/output; disabling restores the original pipeline after this bounded
transition. Existing profiles with controls off never enter the new response. Recenter
resets prediction and output-response state. Invalid tracking holds output and suppresses
prediction; the first returning response sample holds the prior output before convergence.
Output-clock gaps over 250 ms likewise hold the first returning response update.
Center requests arriving during a tracking tick remain pending until processed; the end
of the tick no longer clears a newly arrived request.

**Reset to neutral** restores 42%/55%, disables overrides, and sets all axis fields to 42%.
It preserves the master switch. **OK** saves the existing option bundles; **Cancel** reloads
saved values and original curve points. Queued UI refreshes read the current setting and
block widget feedback, preventing stale reload/toggle events from rewriting live settings.
All existing INI keys remain compatible, including prior Head Movement Controls settings;
only their UI location and nonneutral Speed semantics change. No profile migration is needed.

## Diagnostics and executed validation

Mapping reports live target/effective gain, selected-axis limit and response estimate.
NeuralNet Enhanced Diagnostics and CSV include enabled/override/transition state, six
effective gains, raw/protocol poses, mapped targets, cutoffs, movement limits, source/output
timestamps and validity. For CSV compatibility, `shared_response` means the guarded
adaptive target is available; `fallback_response` means post-mapping response is active.
Timestamps are monotonic host milliseconds. Formatting/export happen outside tracking.

Tests cover neutral/disabled output, nonlinear and asymmetric curves, all six independent
axes, saturation and reduced limits, output timing at 30/60/75/120 FPS, variable/repeated/
regressed timestamps, turns/stops/reversals, recenter/loss, prediction, adaptive interaction,
transitions, settings persistence, queued Qt updates and Mapping save/cancel. Real pipeline
tests exercise both the installed adaptive plugin and a legacy filter. An isolated test
compiles the unchanged production FreeTrack writer and reads its actual six-axis packet,
checking millimeters/radians, signs and unmodified raw fields. It redirects named memory
for isolation and does not initialize registry/game integration.

The original NeuralNet real-model/synthetic-camera tests remain. Current build/test results,
changed files and limitations are in [ENHANCED-COMPLETION.md](ENHANCED-COMPLETION.md).
Build commands and exact CI steps are in [ENHANCED.md](ENHANCED.md). Deploy all rebuilt
application modules together because the additive plugin interface requires matching binaries.
The existing Windows workflow and packaging are unchanged by this Mapping correction.

Physical acceptance still requires a camera and actual game consumer:

1. Record an existing profile with controls off, then enable 42%/55% and compare position
   and response. Open Mapping and confirm original points remain unchanged.
2. Compare the original and orange target at 0%/42%/100%, including saturation, asymmetric
   curves, offsets and pre/post inversion. Verify the same steady-state view in your game.
3. Compare Speed 0/55/100 during slow movement, rapid turns, stops and reversals, keeping
   capture FPS, prediction horizon and smoothing settings fixed. Repeat at advertised FPS.
4. Exercise all axis overrides, Save/Cancel, Reset, recenter and face loss/reacquisition.
5. Repeat with Enhanced Adaptive Filter and another selected filter; test internal NeuralNet
   smoothing and prediction off/on explicitly. Check FreeTrack and TrackIR-compatible games.
6. Export diagnostics; measure physical display latency separately if required.

PS3 Eye hardware, real-face accuracy, actual game consumption and physical latency were
not validated by synthetic software tests. The packet test does not validate a running
NPClient consumer or replace hardware acceptance.

# OpenTrack Enhanced 2.1 — Smart Mapping and racing profiles

Open **Mapping → Smart Mapping…** after Automatic calibration, or choose
**Compare styles, edit points, select axes or save as…** from calibration review.
The existing six-axis Mapping editor remains available. Old profiles load their
existing spline points; calibration records and templates are optional.

## Review and editing

Choose an axis and adjust its positive/negative virtual targets, early sensitivity
(0.5–2), response shape (1.2–3), or physical deadzone. The physical ranges come from
your actual calibration and remain unchanged when switching templates.

The native spline editor displays the original mapping in grey, the draft points
and curve in blue, and the Amplification-adjusted target in orange. A shaded
deadzone, comfortable physical limit and output saturation line show the relevant
boundaries. Left click adds a point, drag moves it, and right click removes it.
Edited curves must start at (0,0), remain monotonic under OpenTrack's actual spline
evaluator, and stay inside the measured physical range. Invalid drafts cannot apply.
**Reset this axis to previous draft** swaps the axis with its preceding draft state.
Changing geometry parameters regenerates the points and replaces manual edits.

**Compare all three styles** displays all three real spline evaluations with the
same physical ranges, targets, sensitivity and gain:

- **Natural:** nearly linear, with a soft center.
- **Balanced:** lower center sensitivity increasing toward the physical endpoint.
- **Responsive:** stronger early response for frequent glances or small movement.

These are preferences, not universally better modes. They change geometry only;
they add no temporal smoothing. Legacy power geometry remains available.

## Editable racing templates

| Template | Virtual yaw each side | Virtual pitch each side | Style |
| --- | ---: | ---: | --- |
| Le Mans Ultimate GT3 | 65° | 25° | Balanced |
| Le Mans Ultimate Hypercar | 55° | 20° | Natural |
| Assetto Corsa GT / Road | 70° | 30° | Balanced |
| Assetto Corsa Drift | 100° | 35° | Responsive |
| Generic Racing | 60° | 25° | Natural |

These numbers are editable starter preferences for virtual camera movement. They
are independent of game FOV, game camera implementation, display size, and monitor
placement. Test comfort and visibility in your own game. Templates change calibrated
yaw/pitch drafts only and do not change camera capture, inference, filters or prediction.

## Amplification, Speed and measured recommendations

The physical probe shows **Physical input → Curve output → Amplification → Final
target** using the same evaluator and output transforms as the pipeline. It includes
branch selection, input inversion, one gain application, saturation, zero position
and output inversion. It is a steady target, not a simulation of prediction or live
filtering. Translation targets require relative translation/neck compensation to
be disabled, and selected axes must use their own input source with mapping enabled.

Amplification **42% means 1×**; disabled Head Movement Controls also use 1×.
Generation divides the requested virtual target by the current gain, so that gain
is applied once in normal tracking. A target that cannot fit the native mapping
output limit at the current gain is rejected. Changing Amplification later changes
the output normally. Existing gain curves and presets are preserved.

Speed controls response timing and never changes a static mapping curve. **55%**
retains the existing filter response. The existing Speed implementation follows a
parallel reference from that filter; Smart Mapping adds no filtering stage.

**Measured recommendations** explains each suggestion: a deadzone from MAD/P95,
noise relative to personal range for center sensitivity and style, average
sensitivity from explicit targets/ranges, conservative optional Enhanced Adaptive
Filter values from P95, and interpretable neutral Amplification/Speed settings.
Recommendations are informational; no controls are silently changed. Sample rate
and reliability do not establish optimal timing or measured performance gains.

## Saving, undo and INI transport

Preview, point edits and template switching do not change live mapping or write
profiles. Check the axes to apply, approve the saving checkbox, then choose:

- **Apply selected axes:** stop tracking and save those branches/limits through
  Mapping's existing persistence path. Other axes and tracking settings remain intact.
- **Save as new profile:** use the existing OpenTrack profile-name dialog to create
  an unused INI. Only selected mappings, their calibration metadata and the reviewed
  Head Movement Controls are written over a copy of the current saved profile.
  Unrelated camera, inference, prediction, filter and unknown legacy keys are retained.
  The active profile is preserved; select the new profile when tracking is stopped.

**Undo calibration / previous mapping** restores the last affected mapping and
record. **Restore original mapping** restores the mappings captured before calibration
and retains an undo snapshot of the state it replaced. These actions explicitly
confirm saving and stop tracking. Mapping's **Reset this axis to previous state**
restores that axis to the state at opening the Mapping dialog; save with OK or discard
with Cancel. Saving a Smart Mapping application is explicit and remains saved if the
outer Mapping dialog is subsequently cancelled.

The profile menu's **Import INI profile…** copies a legacy or current INI to an unused
name in OpenTrack's normal profile directory. It does not activate or migrate the
import. **Export saved INI profile…** exports the last saved profile; unsaved dialog
edits are not included. Both use full INI copying with atomic destination writes,
preserve unknown keys, and guard against copying a file onto itself. Export uses the
file dialog's overwrite confirmation. Smart Mapping never overwrites an existing
Save as destination.

Calibration record schema 2 adds response style, branch sensitivity, generation gain
and manually edited points. Schema 1 still decodes with legacy geometry and 1× gain.
The ordinary mapping groups remain authoritative; loading a record does not reapply it.
The additive `original` snapshot persists alongside the existing `record` and `rollback`.

## Verification

```bat
tools\dev-command.bat cmake --build build --target install --parallel 8
tools\dev-command.bat ctest --test-dir build -R smart-mapping- --output-on-failure --output-junit smart-mapping-tests.xml
tools\dev-command.bat ctest --test-dir build --output-on-failure --output-junit smart-mapping-regression-tests.xml
powershell -NoProfile -File tools\verify-package.ps1 -Stage stage
```

Tests use synthetic accepted measurements and isolated INI files. They cover three
styles across six asymmetric axes, native spline monotonicity/deadzone, gain and Speed,
saturation/signs/zero offset, original/effective overlays, templates, native point
editing, selected application, undo/original restore, schema 1 records, legacy INI
transport, save-as protection, real pipeline output and production FreeTrack/NPClient
packets. No physical PS3 Eye, user comfort, game-camera behavior, monitor geometry or
camera-to-display latency validation is claimed.

### Executed local results — 2026-10-11

- Windows x64 Release build and install: **passed**. Runnable application:
  `stage/opentrack.exe`.
- Full regression suite: **132/132 passed**, 53.96 seconds, including the existing
  NeuralNet 2.0, optical flow, prediction, adaptive/legacy filters and output tests.
  Evidence: `build/smart-mapping-regression-tests.xml`.
- SDK-independent build and core suite: **90/90 passed**, 2.58 seconds.
  Evidence: `build-enhanced/smart-mapping-core-regression-tests.xml`.
- Final checks after adding the low-Amplification review explanation:
  **11/11 Smart Mapping tests** and **7/7 calibration integration tests passed**.
  Evidence: `build/smart-mapping-final-tests.xml` and
  `build/smart-mapping-final-calibration-tests.xml`.
- Package verification: **31 required runtime/document files passed**.
- Qt editor, comparison, recommendations and calibration review snapshots were
  rendered and inspected. Preview, Save as, import/export, selected saving with
  unrelated pending edits, undo, original restore and actual point movement were
  exercised using temporary profiles. Production FreeTrack/NPClient packets passed
  for all three styles, six axes, both branches and 4× Amplification.
- No commit or push was made. Physical camera, comfort and gameplay checks remain
  unperformed; templates are editable starting preferences.

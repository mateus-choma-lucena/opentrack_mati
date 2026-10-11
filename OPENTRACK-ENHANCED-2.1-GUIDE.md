# OpenTrack Enhanced 2.1 guide

## Start with a profile backup

Stop tracking. Copy your current INI using the existing profile **Save as** command
or export it from the main window. Keep a named backup such as `before-calibration.ini`.
Calibration is optional and old profiles load without enabling it. Camera, model,
prediction and protocol settings are preserved when selected mappings are saved.

## PS3 Eye and NeuralNet 2.0

1. Connect the PS3 Eye and close other programs using it. In NeuralNet camera settings,
   select **PS3 Eye open driver**, **640 × 480**, **75 FPS**. The bundled open backend
   needs the camera's video interface accessible through its existing libusb driver.
   If it cannot open, inspect the driver and USB connection before changing tracking.
2. Start tracking and confirm a visible face, stable lighting and the actual capture
   rate in diagnostics. Requested FPS and delivered FPS are different measurements.
   Exposure must be short enough for movement; reduce exposure and improve lighting
   if turns blur. Keep gain as low as practical. Camera settings offer automatic
   exposure/gain, but consistent manual lighting is useful during calibration.
3. In NeuralNet settings → **Enhanced**, enable **NeuralNet 2.0** and **pose validation**.
   Keep the current ONNX model initially. All five models are included; change models
   only after comparing actual inference time and tracking quality on your system.
4. Select **Enhanced Adaptive Filter** if you want motion-aware stabilization.
   Dynamic Prediction 2.0, optical flow and the live analyzer remain independent
   options. Begin with your existing working settings; calibration does not change them.
5. Enable mapping and centering in Options; disable automatic centering. Calibrated
   axes must use their own tracker source. Optional translation additionally requires
   relative translation and neck compensation to be disabled. The wizard explains
   incompatible settings instead of changing them automatically.

See [NeuralNet 2.0](NEURALNET-2.md) and
[experiments and diagnostics](NEURALNET-2-EXPERIMENTS.md) for detailed controls.

## Automatic calibration

Open **Mapping → Automatic calibration…** while tracking.

1. **Welcome:** wait for reliable validated physical tracking. The live indicator
   reports degrees and centimeters before filtering, prediction or mapping.
2. **Center:** sit normally, face forward and press **Record / Retry**. The ordinary
   OpenTrack Center operation runs; allow its settling interval and hold for two seconds.
3. **Stationary noise:** hold neutral for at least four seconds. Breathe normally.
   Deliberate movement, excessive drift or unreliable capture requires a retry.
4. **Left, right, up, down:** move gently to each comfortable endpoint **before**
   pressing Record. Hold for at least two seconds and explicitly confirm comfort.
   Each recording also needs 60 unique valid samples. Never force your neck.
5. **Optional axes:** include roll or small X/Y/Z movements only if useful. Skip
   optional axes individually or skip the whole optional section. Both signed branches
   must be measured for every applied axis.
6. **Game profile:** choose personal targets or a racing starting point. These specify
   virtual camera movement, not how far your head should turn.
7. **Compare:** select an axis and response style. The plot evaluates the actual native
   splines with the same physical ranges, targets and current Amplification.
8. **Review:** check physical negative/positive ranges, virtual targets, deadzone and
   shape. Check only axes to replace. Use **Smart Mapping** for individual points,
   sensitivity, original comparisons, recommendations and saving a separate new INI.
9. Approve the selected mappings and press **Apply and continue**. Tracking stops,
   selected mappings are saved and rollback snapshots are retained. A save failure
   restores the prior mappings and leaves the wizard open for retry.
10. **Completion:** Finish closes the wizard. Restart tracking and center again. If
    you saved a separate INI instead, stop tracking and select that profile first.

Tracking loss pauses the segment. After 0.5 seconds of reliable tracking, recording
automatically restarts from an empty segment. Return gently to the requested position.
**Back** to a recording page clears that recording and later dependent measurements.
Recenter or a new tracking session clears the calibration and returns to Welcome.
Cancel before Apply discards the draft. Tab moves focus; Alt+R records/retries,
Alt+C confirms comfort, and native Back/Next/Cancel/Finish buttons have keyboard mnemonics.
Page content scrolls on small displays or at high DPI; navigation stays visible.

## Styles and racing examples

| Style | Geometry | Starting use |
| --- | --- | --- |
| Natural | Near linear, soft neutral | Small, deliberate cockpit glances |
| Balanced | Lower sensitivity near neutral | GT cockpit viewing |
| Responsive | Earlier turning response | Frequent larger side glances |

These are curve geometries, not additional temporal filters. Try each preview; keep
the style that makes small glances comfortable without sacrificing useful side vision.

| Editable template | Virtual yaw, each side | Virtual pitch, each side | Style |
| --- | ---: | ---: | --- |
| Le Mans Ultimate GT3 | 65° | 25° | Balanced |
| Le Mans Ultimate Hypercar | 55° | 20° | Natural |
| Assetto Corsa GT / Road | 70° | 30° | Balanced |
| Assetto Corsa Drift | 100° | 35° | Responsive |
| Generic Racing | 60° | 25° | Natural |

Personal yaw starts at 150° each way. Templates are editable preferences and make no
assumptions about game FOV, monitor placement or game camera limits. A physical left
range of 20° and right range of 30° can produce the same 65° virtual target on each
side through different native branches. The game can further limit its cockpit camera.

## Amplification and Speed

**Mapping → Head Movement Controls** owns these controls. Its master switch is
optional. Amplification **42% = 1×**; **0% = 0.25×**, **100% = 4×**. Per-axis values
replace the global gain. Calibration divides requested targets by current gain in
the stored points; the runtime applies gain once. Changing gain later changes output.
Unreachable targets are blocked; lower the target, or Cancel and deliberately adjust
gain in Head Movement Controls before calibrating again. Mapping limits, zero offsets
and final inversion still apply.

**Speed 55%** keeps existing filter timing. Other values control convergence to the
mapped target separately. Speed does not change the steady-state curve. Test gain and
Speed separately so you can distinguish larger turning from faster response.
See [Head Movement Controls](HEAD-MOVEMENT-CONTROLS.md) for exact formulas.

## Exact LMU calibration test

1. Back up the INI. Close LMU and any competing TrackIR software. Select **NeuralNet**
   input, **Enhanced Adaptive Filter**, and **freetrack 2.0 Enhanced** output. In the
   output settings select **Enable both** for FreeTrack and TrackIR.
2. Start tracking. Verify 640 × 480 / 75 FPS camera selection, NN2 validation and
   reliable face tracking. Assign an accessible OpenTrack Center shortcut.
3. Run Automatic calibration through the four comfortable yaw/pitch endpoints.
   Skip optional axes for the first test. Select **Le Mans Ultimate GT3**; compare
   Natural/Balanced/Responsive. Use Balanced initially, yaw 65° and pitch 25° each side.
4. Check yaw and pitch on Review, approve and Apply. Finish, restart tracking and
   press Center while seated in the normal driving position.
5. Launch an up-to-date LMU with native TrackIR support and enter an offline practice
   session in cockpit view. Native support was introduced in v1.1; early v1.1 hotfixes
   addressed TrackIR issues. See the [official announcement](https://lemansultimate.com/lmu-adds-first-elms-content-and-custom-livery-upgrades/)
   and [official patch history](https://guide.lemansultimate.com/hc/en-gb/sections/14836404578575-Version-1-1).
6. While stationary, test neutral, small left/right glances, both comfortable yaw
   endpoints and up/down. Compare OpenTrack's physical input and mapped output;
   endpoints should reach the requested effective targets before game-side limits.
   Confirm direction and center; use the existing axis inversion controls if needed.
7. Briefly cover the camera, then uncover it. Check the loss warning and smooth
   recovery. Center again if your seating position changed. Drive one slow practice
   lap only after the stationary test feels comfortable.
8. If camera movement is too large, lower virtual targets. If response is too quick,
   compare Natural/Balanced or change Speed separately. Repeat the stationary test.
   **Mapping → Undo calibration / previous mapping** restores the affected curves.

LMU was not run by this automated validation. Its menu labels and camera restrictions
can vary by version; no unverified in-game toggle or plugin-installation step is required
by this guide. If the game does not connect, update LMU, start OpenTrack before launching
it, check both output interfaces, and check that competing tracking software is closed.

For the original **Assetto Corsa**, use the same output and cockpit practice sequence;
choose GT / Road or Drift and begin with yaw/pitch only. These templates do not imply
testing in Assetto Corsa Competizione or EVO.

## Backups, undo and troubleshooting

Apply retains the previous affected mappings and a separate original snapshot.
**Undo calibration / previous mapping** restores the prior points, limits and alternate
branch flags. **Restore original mapping** restores the original snapshot after approval.
Smart Mapping **Save as new INI** copies existing settings and changes only reviewed
mapping/head-control keys; it does not overwrite an existing file or switch profiles.

| Symptom | Check |
| --- | --- |
| Welcome never becomes ready | Tracking running, NN2 and validation enabled, face visible, reliability, camera permission/driver |
| Repeated automatic retry | Lighting, exposure blur, lost face, stale timestamps, accidental motion; reposition before Record |
| Same direction rejected | Measure the opposite physical direction; branch signs follow existing input inversion |
| Stationary test rejected | Return to relaxed neutral; avoid chair/camera movement; do not hide drift with a larger deadzone |
| Optional translation blocked | Own-axis source, relative translation disabled, neck compensation disabled |
| Apply disabled | Valid measured axes selected, reachable virtual targets and overwrite approval checked |
| Profile cannot save | Writable profile directory; mappings are restored on failure; retry or save a new INI |
| PS3 Eye cannot open/reconnect | Other camera programs, USB connection and installed video-interface driver; stop/restart tracking |
| Game moves differently from preview | Game camera limits, final inversion/zero offset, changed Amplification, cockpit view/output connection |

Before keeping a profile, test neutral jitter, slow glances, rapid turns/stops,
reversal, comfortable endpoints, loss/recovery, recenter, restart, and Undo. Observe
diagnostics rather than treating a selected 75 FPS mode as proof of delivered capture.

## Validation scope

[Release report](RELEASE-2.1-REPORT.md) records actual builds, regression totals,
physical capture measurements, synthetic processing measurements and remaining limits.
Automated camera reconnect tests use a deterministic backend; the physical probe tests
stop/reopen and shutdown without unplugging the device. Performance probes save no video.

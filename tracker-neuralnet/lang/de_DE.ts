<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="de_DE">
<context>
    <name>Form</name>
    <message>
        <source>Tracker settings</source>
        <translation>Tracker-Einstellungen</translation>
    </message>
    <message>
        <source>Head Center Offset</source>
        <translation>Versatz zur Kopfmitte</translation>
    </message>
    <message>
        <source>Right</source>
        <translation>Rechts</translation>
    </message>
    <message>
        <source>Forward</source>
        <translation>Vorwärts</translation>
    </message>
    <message>
        <source>Up</source>
        <translation>Hoch</translation>
    </message>
    <message>
        <source> mm</source>
        <translation> mm</translation>
    </message>
    <message>
        <source>Use only yaw and pitch while calibrating.
Don&apos;t roll or change position.</source>
        <translation>Während der Kalibrierung nur gieren und nicken.
Bitte nicht rollen oder die Position ändern.</translation>
    </message>
    <message>
        <source>Start calibration</source>
        <translation>Kalibrierung starten</translation>
    </message>
    <message>
        <source>Internal Noise Filter</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Face Crop Options:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Smoothing Alpha</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Show</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Zoom</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Camera Configuration</source>
        <translation>Kamera-Konfiguration</translation>
    </message>
    <message>
        <source>Diagonal FOV</source>
        <translation>Diagonales Sichtfeld</translation>
    </message>
    <message>
        <source>Camera name</source>
        <translation>Kamera-Name</translation>
    </message>
    <message>
        <source>Field of view. Needed to transform the pose to world coordinates.</source>
        <translation>Sichtfeld. Benötigt, um die Pose in Welt-Koordinaten zu übersetzen.</translation>
    </message>
    <message>
        <source>The requested resolution for cases where the camera delivers maximum frame rate only for a particular resolution. The image may still be downscaled to the internal resolution.</source>
        <translation>Die angeforderte Auflösung für Fälle, in denen die Kamera die maximale Bildrate nur bei bestimmten Auflösungen ausgibt. Das Bild wird möglicherweise weiterhin herunterskaliert auf die interne Auflösung.</translation>
    </message>
    <message>
        <source>Resolution</source>
        <translation>Auflösung</translation>
    </message>
    <message>
        <source>Requested video frame rate. Actual setting may not be supported by the camera.</source>
        <translation>Angeforderte Bildrate. Die tatsächliche Einstellungen wird von der Kamera möglicherweise nicht unterstützt.</translation>
    </message>
    <message>
        <source>Frames per second</source>
        <translation>Bilder pro Sekunde</translation>
    </message>
    <message>
        <source>MJPEG</source>
        <translation>MJPEG</translation>
    </message>
    <message>
        <source>Camera settings</source>
        <translation>Kamera-Einstellungen</translation>
    </message>
    <message>
        <source>Tuning / Debug</source>
        <translation>Tuning / Fehlersuche</translation>
    </message>
    <message>
        <source>Thread Count</source>
        <translation>Anzahl der Threads</translation>
    </message>
    <message>
        <source>Number of threads. Can be used to balance the CPU load between the game and the tracker.</source>
        <translation>Anzahl der Threads. Kann verwendet werden, um die CPU-Last zwischen Spiel und Tracker zu balancieren.</translation>
    </message>
    <message>
        <source>Show the image patch that the pose estimation model sees.</source>
        <translation>Zeigt den Bildausschnitt, den das Modell zur Posenabschätzung sieht.</translation>
    </message>
    <message>
        <source>Amount of smoothing of the face region coordinates. Can help stabilize the pose.</source>
        <translation>Umfang der Glättung der Gesichtkoordinaten. Kann helfen, die Pose zu stabilisieren.</translation>
    </message>
    <message>
        <source>Zoom factor for the face region. Applied before the patch is fed into the pose estimation model. There is a sweet spot near 1.</source>
        <translation>Zoom-Faktor der Gesichtsregion. Wird angewendet, bevor der Bildausschnitt zum Posen-Abschätzungsmodell gesendet wird. Der Sweet-Spot liegt nahe bei 1.</translation>
    </message>
    <message>
        <source>Select the pose network. Changes take affect on the next tracker start</source>
        <translation>Wählt das Pose-Netzwerk. Die Änderungen treten beim nächsten Start des Trackers inkraft</translation>
    </message>
    <message>
        <source>Select Pose Net ONNX</source>
        <translation>Wähle Pose-Netzwerk ONNX</translation>
    </message>
    <message>
        <source>&lt;the pose net file&gt;</source>
        <translation>&lt;die pose netzwerk datei&gt;</translation>
    </message>
</context>
<context>
    <name>neuralnet_tracker_ns::NeuralNetDialog</name>
    <message>
        <source>Default</source>
        <translation>Standard</translation>
    </message>
    <message>
        <source>%1x%2 @ %3</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>USB identity unavailable</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>USB VID:PID %1:%2</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>This backend did not enumerate modes. Resolution and FPS above are requests, subject to driver negotiation.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Backend modes: %1 FPS. Availability also depends on USB bandwidth.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>, </source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Tracker Offline</source>
        <translation>Tracker offline</translation>
    </message>
    <message>
        <source>%1x%2 @ %3 FPS / Inference: %4 ms</source>
        <translation>%1x%2 @ %3 FPS / Inferenz: %4 ms</translation>
    </message>
    <message>
        <source>Requested: %1 FPS; capture delivery: %2 FPS; inference pipeline: %3 FPS
Inference: %4 ms; host frame age: %5 ms; host processing latency: %6 ms
Latency P50 / P95 / P99: %7 / %8 / %9 ms (last 256 samples)
Capture interval mean / P95: %10 / %11 ms
Queue replacements: %12; stale frames: %13; backend sequence gaps: %14
Connected: %15; tracking valid: %16; reconnects: %17
Internal filter: %18; adaptive filter: %19; applied prediction: %20 ms (%21 degrees / %22 cm)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>N/A</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>yes</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>no</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>on</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>off</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>
Host capture to protocol submission: %1 ms; P50 / P95 / P99: %2 / %3 / %4 ms</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>
NeuralNet 2.0: %1; state: %2; estimated reliability: %3 (not model confidence)
Rejected / spikes / reacquisition attempts (session totals): %4 / %5 / %6
Current loss: %7 ms; motion: %8; hold: %9; recovery: %10
Raw / validated host timestamp: %11 / %12 ms; capture-to-pose age: %13 ms; validation/recovery: %14 ms
Motion stabilization requested / adaptive filter active: %15 / %16</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Tracker offline; measurements unavailable.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>%1 yaw samples. Yaw more to %2 samples for stable calibration.</source>
        <translation>%1 Gieren-Proben. Weiterhin gieren bis %2 Proben für eine stabile Kalibrierung.</translation>
    </message>
    <message>
        <source>%1 pitch samples. Pitch more to %2 samples for stable calibration.</source>
        <translation>%1 Nicken-Proben. Weiterhin nicken bis %2 Proben für eine stabile Kalibrierung.</translation>
    </message>
    <message>
        <source>%1 samples. Over %2, good!</source>
        <translation>%1 Proben. Mehr als %2, gut!</translation>
    </message>
    <message>
        <source>Stop calibration</source>
        <translation>Kalibrierung stoppen</translation>
    </message>
    <message>
        <source>Start calibration</source>
        <translation>Kalibrierung starten</translation>
    </message>
    <message>
        <source>Select Pose Net ONNX</source>
        <translation>Wähle Pose-Netzwerk ONNX</translation>
    </message>
    <message>
        <source>ONNX Files (*.onnx)</source>
        <translation>ONNX-Dateien (*.onnx)</translation>
    </message>
    <message>
        <source>Enhanced</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Performance</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Latest-frame capture worker (restart tracking to apply)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Uses one pending frame. Older pending frames are replaced; inference quality and model resolution remain unchanged.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Maximum host frame age</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source> ms</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Automatically reconnect camera</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Maximum consecutive retries</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>NeuralNet 2.0</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Enable NeuralNet 2.0</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Optional temporal validation and recovery. Disabled by default; existing profiles keep their original processing.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Pose Validation</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Rejects corrupt poses and isolated jumps using acquisition time and short pose history. A continuous second sample can confirm fast movement.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Motion-Aware Stabilization</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Shares motion state with the enabled Enhanced Adaptive Filter. Select that filter and enable it to activate smoothing; no additional tracker smoother is stacked.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Tracking Recovery</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Time-based TRACKING / SUSPECT / LOST / REACQUIRING states. Short holds retain the original timestamp; prediction stops during holds and recovery.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Reliability-Based Protection</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Uses observed continuity and frame age for conservative stationary-history protection. Estimated reliability is not calibrated NeuralNet confidence.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Spike Rejection Strength</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Higher values detect smaller isolated jumps. Continuous movement is confirmed without permanently clamping it.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Recovery Responsiveness</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Scales confirmation time and bounded quaternion/translation recovery speed. 1.0 is the conservative default.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Hold Duration</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Maximum hold since the last good pose and the first failed observation. 0 disables holding. Camera disconnect never holds a pose.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Advanced Settings</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Angular velocity limit</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source> degrees/s</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Translation velocity limit</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source> cm/s</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Suspect to lost timeout</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Reacquisition confirmation</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Recovery blend duration</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Maximum pose age (serial capture)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Serial mode age limit. Latest-frame mode uses Maximum host frame age from Performance. All timestamps start at host receipt, not sensor exposure.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Adaptive Smoothness</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>PS3 Eye Camera</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Exposure / gain / automatic exposure</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Prediction</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Enable motion prediction after filtering</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Prediction horizon</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Maximum angular extrapolation</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source> degrees</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Maximum translation extrapolation</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source> cm</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Diagnostics</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Show performance measurements</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Export diagnostics snapshot (CSV)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Export diagnostics</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>CSV files (*.csv)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Frame age and latency begin at host receipt. Processing ends at pose publication; application latency ends after the first protocol submission of each sample. Camera buffering, exposure, USB/output transit and display latency are unavailable.</source>
        <translation type="unfinished"></translation>
    </message>
</context>
</TS>

param([Parameter(Mandatory=$true)][string]$Stage)
$ErrorActionPreference = 'Stop'
$packageRoot = (Resolve-Path -LiteralPath $Stage).Path
$required = @(
    'opentrack.exe', 'opentrack-api.dll', 'opentrack-logic.dll', 'opentrack-user-interface.dll', 'opentrack-spline.dll',
    'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll',
    'platforms/qwindows.dll', 'vcomp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'modules/onnxruntime.dll',
    'modules/opentrack-tracker-neuralnet.dll', 'modules/opentrack-filter-enhanced-adaptive.dll',
    'modules/opentrack-video-opencv.dll', 'modules/opentrack-video-ps3eye.dll',
    'modules/ps3eye-subprocess.exe', 'modules/libusb-1.0.dll',
    'opentrack-compat.dll', 'opentrack-options.dll', 'opentrack-input.dll', 'opentrack-video.dll', 'opentrack-migration.dll', 'opentrack-pose-widget.dll',
    'modules/opentrack-proto-freetrack.dll', 'modules/NPClient.dll', 'modules/NPClient64.dll', 'modules/freetrackclient.dll', 'modules/freetrackclient64.dll', 'modules/TrackIR.exe',
    'doc/runtime-dependencies.json', 'doc/OPENTRACK-ENHANCED-2.1-GUIDE.md', 'doc/RELEASE-2.1-REPORT.md',
    'modules/models/head-localizer.onnx', 'modules/models/head-pose-0.2-big.onnx',
    'modules/models/head-pose-0.2-small.onnx', 'modules/models/head-pose-0.5-big.onnx',
    'modules/models/head-pose-0.5-small.onnx', 'doc/HEAD-MOVEMENT-CONTROLS.md', 'doc/NEURALNET-2.md', 'doc/NEURALNET-2-EXPERIMENTS.md', 'doc/AUTOMATIC-CALIBRATION.md', 'doc/SMART-MAPPING.md'
)
foreach ($relative in $required) {
    $file = Join-Path $packageRoot $relative
    if (!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -eq 0) {
        throw "Required runtime file missing or empty: $relative"
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $packageRoot 'doc/runtime-dependencies.json') -Raw | ConvertFrom-Json
if ($manifest.schema -ne 1 -or $manifest.pointer_bits -ne 64 -or $manifest.opencv_linkage -notin @('static','shared')) { throw 'Invalid Windows x64 runtime dependency manifest' }
$openCvWorld = @(Get-ChildItem -LiteralPath (Join-Path $packageRoot 'modules') -Filter 'opencv_world*.dll' -File | Where-Object { $_.Length -gt 0 })
if ($manifest.opencv_linkage -eq 'shared' -and $openCvWorld.Count -eq 0) {
    foreach ($component in @('core','imgproc','video')) {
        $componentFiles = @(Get-ChildItem -LiteralPath (Join-Path $packageRoot 'modules') -Filter "opencv_$component*.dll" -File | Where-Object { $_.Length -gt 0 -and $_.Name -match "^opencv_${component}[0-9]+d?\.dll$" })
        if ($componentFiles.Count -eq 0) { throw "Required OpenCV optical-flow runtime component missing: $component" }
    }
}
foreach ($relative in @('opentrack.exe','opentrack-user-interface.dll','Qt6Core.dll','modules/onnxruntime.dll','modules/opentrack-tracker-neuralnet.dll','modules/opentrack-filter-enhanced-adaptive.dll','modules/opentrack-video-ps3eye.dll','modules/NPClient64.dll','modules/freetrackclient64.dll')) {
    $binary = [IO.File]::ReadAllBytes((Join-Path $packageRoot $relative))
    $peOffset = [BitConverter]::ToInt32($binary,60)
    if ($peOffset -lt 0 -or $peOffset+6 -gt $binary.Length -or [BitConverter]::ToUInt32($binary,$peOffset) -ne 0x4550 -or [BitConverter]::ToUInt16($binary,$peOffset+4) -ne 0x8664) { throw "Expected x64 PE runtime: $relative" }
}
Write-Host "OpenCV $($manifest.opencv_version): $($manifest.opencv_linkage) linkage; x64 runtime verified"
Write-Host "Verified $($required.Count) required runtime files at $packageRoot"

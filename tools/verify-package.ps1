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
    'modules/models/head-localizer.onnx', 'modules/models/head-pose-0.2-big.onnx',
    'modules/models/head-pose-0.2-small.onnx', 'modules/models/head-pose-0.5-big.onnx',
    'modules/models/head-pose-0.5-small.onnx', 'doc/HEAD-MOVEMENT-CONTROLS.md', 'doc/NEURALNET-2.md', 'doc/NEURALNET-2-EXPERIMENTS.md'
)
foreach ($relative in $required) {
    $file = Join-Path $packageRoot $relative
    if (!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -eq 0) {
        throw "Required runtime file missing or empty: $relative"
    }
}
$openCvWorld = @(Get-ChildItem -LiteralPath (Join-Path $packageRoot 'modules') -Filter 'opencv_world*.dll' -File | Where-Object { $_.Length -gt 0 })
if ($openCvWorld.Count -eq 0) {
    foreach ($component in @('core','imgproc','video')) {
        $componentFiles = @(Get-ChildItem -LiteralPath (Join-Path $packageRoot 'modules') -Filter "opencv_$component*.dll" -File | Where-Object { $_.Length -gt 0 -and $_.Name -match "^opencv_${component}[0-9]+d?\.dll$" })
        if ($componentFiles.Count -eq 0) { throw "Required OpenCV optical-flow runtime component missing: $component" }
    }
}
Write-Host "Verified $($required.Count) required runtime files at $packageRoot"

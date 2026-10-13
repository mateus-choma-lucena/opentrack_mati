param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'

# Official MSVC x64 SDK, pinned to the release asset's published SHA-256.
# https://github.com/opencv/opencv/releases/tag/4.12.0
$version = '4.12.0'
$sha256 = 'b753b14d880b9bc8d89d6acd3b665c040baec0211078435432fcae117db707af'
$url = "https://github.com/opencv/opencv/releases/download/$version/opencv-$version-windows.exe"
$root = [IO.Path]::GetFullPath($Destination)
$archive = Join-Path $root "opencv-$version-windows.exe"

try {
    $sevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
    $extractor = if ($sevenZip) { $sevenZip.Source } else { Join-Path $env:ProgramFiles '7-Zip/7z.exe' }
    if (!(Test-Path -LiteralPath $extractor -PathType Leaf)) {
        throw '7-Zip is required to extract the official SDK. Install 7-Zip or add 7z.exe to PATH (GitHub Windows runners include it).'
    }
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    if (!(Test-Path -LiteralPath $archive -PathType Leaf)) {
        Write-Host "Downloading OpenCV $version MSVC x64 SDK: $url"
        Invoke-WebRequest -Uri $url -OutFile $archive
    }
    $actualHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($actualHash -ne $sha256) {
        throw "SDK checksum mismatch: expected $sha256, got $actualHash. Remove '$archive' and rerun to download a clean copy."
    }
    & $extractor x $archive "-o$root" 'opencv/build/*' -y -bso0 -bsp0
    if ($LASTEXITCODE -ne 0) {
        throw "7-Zip extraction failed with exit code $LASTEXITCODE. Check available disk space and permissions for '$root'."
    }

    # Use the concrete package, bypassing the root config's compiler-version
    # dispatch (which does not recognize MSVC 19.51 / Visual Studio 2026).
    $configDirectory = Join-Path $root 'opencv/build/x64/vc16/lib'
    foreach ($relative in @(
        'opencv/build/x64/vc16/lib/OpenCVConfig.cmake',
        'opencv/build/x64/vc16/lib/opencv_world4120.lib',
        'opencv/build/x64/vc16/bin/opencv_world4120.dll',
        'opencv/build/include/opencv2/core.hpp',
        'opencv/build/include/opencv2/imgproc.hpp',
        'opencv/build/include/opencv2/video/tracking.hpp'
    )) {
        $file = Join-Path $root $relative
        if (!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -eq 0) {
            throw "Required SDK file is missing or empty: $file. Rerun extraction from the verified archive."
        }
    }
    $env:OpenCV_DIR = $configDirectory
    if ($env:GITHUB_ENV) {
        "OpenCV_DIR=$configDirectory" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
    }
    Write-Host "OpenCV $version SDK verified. OpenCV_DIR=$configDirectory"
} catch {
    throw "OpenCV $version installation failed: $($_.Exception.Message) Asset: $url"
}

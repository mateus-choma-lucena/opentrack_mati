param(
    [Parameter(Mandatory=$true)][string]$Stage,
    [Parameter(Mandatory=$true)][string]$Destination
)
$ErrorActionPreference = 'Stop'
$packageRoot = (Resolve-Path -LiteralPath $Stage).Path
& (Join-Path $PSScriptRoot 'verify-package.ps1') -Stage $packageRoot
$zipPath = [IO.Path]::GetFullPath($Destination)
if ($zipPath.StartsWith($packageRoot + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'ZIP destination must be outside the staging directory' }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($zipPath)) | Out-Null
Add-Type -AssemblyName System.IO.Compression
$files = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Where-Object {
    $_.Name -notmatch '(^enhanced-.*\.exe$|-(test|tests)\.exe$|^head-controls-.*\.png$|preview.*\.png$|\.(pdb|lib)$)'
})
$fileStream = [IO.File]::Open($zipPath,[IO.FileMode]::Create,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
    $archive = [IO.Compression.ZipArchive]::new($fileStream,[IO.Compression.ZipArchiveMode]::Create,$true)
    try {
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($packageRoot.Length+1).Replace('\','/')
            $entry = $archive.CreateEntry($relative,[IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $file.LastWriteTime
            $source = [IO.File]::OpenRead($file.FullName)
            $target = $entry.Open()
            try { $source.CopyTo($target) } finally { $target.Dispose(); $source.Dispose() }
        }
    } finally { $archive.Dispose() }
} finally { $fileStream.Dispose() }
$readStream = [IO.File]::OpenRead($zipPath)
try {
    $archive = [IO.Compression.ZipArchive]::new($readStream,[IO.Compression.ZipArchiveMode]::Read)
    try {
        if ($archive.Entries.Count -ne $files.Count) { throw 'ZIP entry count differs from source package' }
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($packageRoot.Length+1).Replace('\','/')
            $entry = $archive.GetEntry($relative)
            if (!$entry -or $entry.Length -ne $file.Length) { throw "ZIP entry missing or size differs: $relative" }
        }
    } finally { $archive.Dispose() }
} finally { $readStream.Dispose() }
Write-Host "Created and verified $($files.Count) files: $zipPath ($((Get-Item -LiteralPath $zipPath).Length) bytes)"

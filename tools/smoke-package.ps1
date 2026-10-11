param([Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference = 'Stop'
$launchRoot = (Resolve-Path -LiteralPath $Directory).Path
if (!(Test-Path -LiteralPath (Join-Path $launchRoot 'portable.txt') -PathType Leaf)) { throw 'Smoke test requires an isolated portable installation' }
& (Join-Path $PSScriptRoot 'verify-package.ps1') -Stage $launchRoot
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class OpenTrackSmokeWindow {
    private delegate bool EnumWindowProc(IntPtr window, IntPtr argument);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowProc callback, IntPtr argument);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int maximum);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    public static bool Close(int processId) {
        IntPtr main = IntPtr.Zero;
        EnumWindows((window, argument) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if(owner != processId) return true;
            var text = new StringBuilder(512); GetWindowText(window, text, text.Capacity);
            if(text.ToString().IndexOf("opentrack", StringComparison.OrdinalIgnoreCase) < 0) return true;
            main=window; return false;
        }, IntPtr.Zero);
        return main != IntPtr.Zero && PostMessage(main, 0x0010, IntPtr.Zero, IntPtr.Zero);
    }
}
'@
$application = Start-Process -FilePath (Join-Path $launchRoot 'opentrack.exe') -WorkingDirectory $launchRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $launchRoot 'startup.log') -RedirectStandardError (Join-Path $launchRoot 'startup-error.log')
try {
    if (!$application.WaitForInputIdle(5000)) { throw 'Application did not initialize its event loop' }
    Start-Sleep -Milliseconds 2000
    if (![OpenTrackSmokeWindow]::Close($application.Id)) { throw 'Application main window was not created' }
    if (!$application.WaitForExit(5000)) { throw 'Application did not shut down after its native close event' }
    if ($application.ExitCode -ne 0) { throw "Application exited with $($application.ExitCode)" }
    Write-Host 'Packaged native application startup and normal shutdown: PASS (exit 0)'
} finally {
    if (!$application.HasExited) { Stop-Process -Id $application.Id }
    $application.Dispose()
}

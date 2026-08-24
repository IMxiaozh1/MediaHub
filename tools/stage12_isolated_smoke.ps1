param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,

    [Parameter(Mandatory = $true)]
    [string]$MediaPath,

    [ValidateRange(1.0, 3.0)]
    [double]$ScaleFactor = 1.0,

    [ValidateRange(1, 60)]
    [int]$PlaybackSeconds = 5,

    [switch]$LeaveRunning
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$media = (Resolve-Path -LiteralPath $MediaPath).Path
$executable = Join-Path $package 'MediaHub.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "MediaHub.exe was not found in package directory: $package"
}

$env:QT_PLUGIN_PATH = ''
$env:QML2_IMPORT_PATH = ''
$env:VLC_PLUGIN_PATH = ''
$env:QT_SCALE_FACTOR =
    $ScaleFactor.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$env:Path = "$env:SystemRoot\System32;$env:SystemRoot"

$quotedMedia = '"' + $media.Replace('"', '\"') + '"'
$process = Start-Process -FilePath $executable -ArgumentList $quotedMedia `
    -WorkingDirectory $package -PassThru

Start-Sleep -Seconds $PlaybackSeconds
$process.Refresh()
if ($process.HasExited) {
    throw "MediaHub exited during isolated playback with code $($process.ExitCode)."
}
if (-not $process.Responding) {
    throw "MediaHub stopped responding during isolated playback (PID $($process.Id))."
}

$requiredModules = @(
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Network.dll',
    'Qt6Widgets.dll',
    'libvlc.dll',
    'libvlccore.dll'
)
foreach ($moduleName in $requiredModules) {
    $module = $process.Modules | Where-Object {
        $_.ModuleName -ieq $moduleName
    } | Select-Object -First 1
    if ($null -eq $module) {
        throw "MediaHub did not load required module $moduleName."
    }
    if (-not $module.FileName.StartsWith(
            $package, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "MediaHub loaded $moduleName outside the package: $($module.FileName)"
    }
}

if ($LeaveRunning) {
    Write-Output "Isolated manual check ready: scale $ScaleFactor, PID $($process.Id)."
    exit 0
}

if (-not $process.CloseMainWindow()) {
    throw "MediaHub did not expose a closable main window (PID $($process.Id))."
}
if (-not $process.WaitForExit(10000)) {
    throw "MediaHub did not exit within 10 seconds after a normal close (PID $($process.Id))."
}
if ($process.ExitCode -ne 0) {
    throw "MediaHub returned exit code $($process.ExitCode) after a normal close."
}

Write-Output "Isolated playback smoke passed: scale $ScaleFactor, PID $($process.Id), exit code 0."

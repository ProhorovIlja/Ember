# One explicit Windows invocation: compile native Ember, deploy Qt runtime,
# require local voice/LLM/ASR assets, and assemble installer plus portable ZIP.
# There are NO downloads, scheduled tasks, background builds or CI triggers.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$AssetsDirectory,
    [Parameter(Mandatory=$true)][string]$ApprovedManifest,
    [Parameter(Mandatory=$true)][string]$DistributionDirectory,
    [Parameter(Mandatory=$true)][string]$Version,
    [string]$EnglishWav = '',
    [string]$EnglishReference = '',
    [string]$RussianWav = '',
    [string]$RussianReference = '',
    [ValidateRange(0.0,1.0)][double]$MaxAsrWer = 0.25,
    [switch]$SkipLiveEngineChecks
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$qt = (Resolve-Path -LiteralPath $QtRoot -ErrorAction Stop).Path
$deploy = Join-Path $qt 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $deploy -PathType Leaf)) {
    throw 'Installed Qt6 MSVC x64 windeployqt.exe not found at the selected Qt root.'
}
if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue) -or
    -not (Get-Command ctest.exe -ErrorAction SilentlyContinue)) {
    throw 'Installed Visual Studio 2022/CMake/CTest are required on the Windows build computer.'
}
$build = [System.IO.Path]::GetFullPath($BuildDirectory)
if (Test-Path -LiteralPath $build) {
    throw 'BuildDirectory already exists: refusing to overwrite prior work.'
}
$target = [System.IO.Path]::GetFullPath($DistributionDirectory)
if (Test-Path -LiteralPath $target) {
    throw 'DistributionDirectory already exists.'
}
# Keep clean-source build and output separate from user model/asset trees.
$assetPath = (Resolve-Path -LiteralPath $AssetsDirectory -ErrorAction Stop).Path
foreach ($base in @($root, $assetPath, $qt)) {
    $prefix = $base.TrimEnd([char[]]@('\','/')) + [System.IO.Path]::DirectorySeparatorChar
    if ($build.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        $target.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        $base.StartsWith(($build.TrimEnd([char[]]@('\','/')) +
            [System.IO.Path]::DirectorySeparatorChar),
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Build/output folders must not overlap source, installed Qt or model assets.'
    }
}
if ($build -ieq $target) { throw 'Build and distribution folders must be different.' }
if ($target.StartsWith(($build.TrimEnd([char[]]@('\','/')) +
        [System.IO.Path]::DirectorySeparatorChar),
        [System.StringComparison]::OrdinalIgnoreCase) -or
    $build.StartsWith(($target.TrimEnd([char[]]@('\','/')) +
        [System.IO.Path]::DirectorySeparatorChar),
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Build and distribution trees must not overlap.'
}
# One foreground compile, no GitHub Actions and no network setup.
& cmake.exe -S $root -B $build -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_PREFIX_PATH=$qt" '-DBUILD_TESTING=ON'
if ($LASTEXITCODE -ne 0) { throw 'Native Qt CMake configuration failed.' }
& cmake.exe --build $build --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Native Ember compilation failed.' }
& ctest.exe --test-dir $build -C Release
if ($LASTEXITCODE -ne 0) { throw 'Native Ember regression tests failed.' }

$app = Join-Path $build 'Release'
$exe = Join-Path $app 'Ember.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw 'CMake produced no native Windows Ember.exe.'
}
& $deploy --release --qmldir (Join-Path $root 'qml') $exe
if ($LASTEXITCODE -ne 0) { throw 'Qt dynamic runtime deployment failed.' }
& (Join-Path $root 'release/stage-msvc-applocal.ps1') `
    -PayloadDirectory $app -PruneDevelopmentTools
if (-not $?) { throw 'App-local Microsoft VC++ runtime staging failed.' }
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $app 'LICENSE')
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination (Join-Path $app 'README.md')
Copy-Item -LiteralPath (Join-Path $root 'docs/SETUP.md') -Destination (Join-Path $app 'SETUP.md')
$arguments = @{
    ApplicationDirectory = $app
    AssetsDirectory = $assetPath
    ApprovedManifest = $ApprovedManifest
    DistributionDirectory = $target
    Version = $Version
    EnglishWav = $EnglishWav
    EnglishReference = $EnglishReference
    RussianWav = $RussianWav
    RussianReference = $RussianReference
    MaxAsrWer = $MaxAsrWer
}
if ($SkipLiveEngineChecks) { $arguments.SkipLiveEngineChecks = $true }
& (Join-Path $root 'release/build-complete-offline.ps1') @arguments
if (-not $?) { throw 'Complete offline Ember packaging failed.' }
Write-Host 'Native Qt build, all regression tests and offline assembly completed.'
Write-Host 'Real end-user quality/listening acceptance must still be separately approved.'

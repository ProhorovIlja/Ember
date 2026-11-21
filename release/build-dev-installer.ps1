param(
    [string]$PayloadDirectory = 'build/Release',
    [string]$OutputDirectory = 'dist',
    [string]$Version = '0.1.0-dev',
    [switch]$VerifyInstall
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($Version -notmatch '^([0-9]+[.][0-9]+[.][0-9]+)(-dev)?$') {
    throw 'Version must be MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-dev.'
}
$numericVersion = $Matches[1] + '.0'
$root = Split-Path -Parent $PSScriptRoot
$payload = (Resolve-Path (Join-Path $root $PayloadDirectory)).Path
$output = Join-Path $root $OutputDirectory
New-Item -ItemType Directory -Path $output -Force | Out-Null
if (!(Test-Path (Join-Path $payload 'Ember.exe'))) { throw 'Native EXE missing.' }
if (!(Test-Path (Join-Path $payload 'Qt6Core.dll'))) { throw 'Qt runtime missing.' }
if (!(Test-Path (Join-Path $payload 'LICENSE'))) { throw 'License file missing.' }

$compilerCandidates = @(
    (Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Inno Setup 6\ISCC.exe'),
    (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'Inno Setup 6\ISCC.exe')
)
$compiler = $compilerCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (!$compiler) {
    $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($cmd) { $compiler = $cmd.Source }
}
if (!$compiler) {
    choco install innosetup -y --no-progress
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup installation failed.' }
    $compiler = $compilerCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (!$compiler) { throw 'Inno Setup 6 ISCC.exe not found.' }
$iss = Join-Path $PSScriptRoot 'ember.iss'
& $compiler "/DAppPayload=$payload" "/DAppOutput=$output" "/DBuildVersion=$Version" "/DBuildNumericVersion=$numericVersion" $iss
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
$installerName = "Ember-$Version-Windows-x64.exe"
$installer = Join-Path $output $installerName
if (!(Test-Path $installer)) { throw "Installer missing: $installer" }
$hash = (Get-FileHash -Algorithm SHA256 -Path $installer).Hash.ToLowerInvariant()
$checksumName = if ($Version.EndsWith('-dev')) { 'Ember-dev-installer-SHA256.txt' } else { "Ember-$Version-SHA256.txt" }
Set-Content -Path (Join-Path $output $checksumName) -Value ($hash + '  ' + $installerName) -Encoding ascii
Write-Host "Unsigned development installer created. SHA256: $hash"

if ($VerifyInstall) {
    $install = Join-Path $env:RUNNER_TEMP 'Ember-Installer-Smoke'
    if (Test-Path $install) { throw "Refusing to overwrite existing folder: $install" }
    $args = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', ('/DIR="' + $install + '"'))
    $setup = Start-Process -FilePath $installer -ArgumentList $args -PassThru -Wait
    if ($setup.ExitCode -ne 0) { throw "Silent install failed: $($setup.ExitCode)" }
    $installedExe = Join-Path $install 'Ember.exe'
    if (!(Test-Path $installedExe)) { throw 'Ember.exe missing after install.' }
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QUICK_BACKEND = 'software'
    $env:QT_OPENGL = 'software'
    $smoke = Start-Process -FilePath $installedExe -ArgumentList '--smoke-test' -PassThru -Wait
    $diagnostic = Join-Path $install 'qml-smoke-report.txt'
    if (Test-Path $diagnostic) { Get-Content $diagnostic }
    if ($smoke.ExitCode -ne 0) { throw "Installed app could not open: $($smoke.ExitCode)" }
    $uninstaller = Get-ChildItem -LiteralPath $install -Filter 'unins*.exe' | Select-Object -First 1
    if (!$uninstaller) { throw 'Uninstaller not found.' }
    $uninstall = Start-Process -FilePath $uninstaller.FullName -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -PassThru -Wait
    if ($uninstall.ExitCode -ne 0) { throw "Silent uninstall failed: $($uninstall.ExitCode)" }
    if (Test-Path $installedExe) { throw 'Uninstall left Ember.exe behind.' }
    Write-Host 'Per-user install, real QML launch, and uninstall smoke passed.'
}

param(
    [string]$PayloadDirectory = 'build/Release',
    [string]$OutputDirectory = 'dist',
    [string]$Version = '0.1.0-dev',
    [switch]$VerifyInstall,
    [switch]$RequireOfflineBundle,
    [switch]$RequireVoiceBundle
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($Version -notmatch '^([0-9]+[.][0-9]+[.][0-9]+)(-dev)?$') {
    throw 'Version must be MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-dev.'
}
$numericVersion = $Matches[1] + '.0'
$root = Split-Path -Parent $PSScriptRoot
# Accept both repository-relative CI paths and absolute user-selected offline
# assembly paths, including large payloads staged on another drive.
$payloadPath = if ([System.IO.Path]::IsPathRooted($PayloadDirectory)) {
    $PayloadDirectory
} else {
    Join-Path $root $PayloadDirectory
}
$payload = (Resolve-Path $payloadPath).Path
$output = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory
} else {
    Join-Path $root $OutputDirectory
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
if (!(Test-Path (Join-Path $payload 'Ember.exe'))) { throw 'Native EXE missing.' }
if (!(Test-Path (Join-Path $payload 'Qt6Core.dll'))) { throw 'Qt runtime missing.' }
if (!(Test-Path (Join-Path $payload 'LICENSE'))) { throw 'License file missing.' }
# Inno's recursive [Files] section copies the entire payload. Refuse a
# previously executed app directory rather than accidentally publish chats,
# model prompts, microphones or session backups in a release installer.
$payloadItem = Get-Item -LiteralPath $payload -Force
if (($payloadItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'Installer payload root must not be a link or junction.'
}
# Inno's wildcard [Files] includes nested Qt files and all hidden files.
# Reject reparse-point links and secrets recursively before invoking ISCC.
foreach ($item in Get-ChildItem -LiteralPath $payload -Recurse -Force) {
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Installer payload contains a symlink or junction.'
    }
    $relative = $item.FullName.Substring($payload.Length).TrimStart([char]'\', [char]'/')
    foreach ($segment in ($relative -split '[\\/]')) {
        $lower = $segment.ToLowerInvariant()
        if ($lower -in @('data', 'cache', '.git', '.ssh', '.aws', '.env',
                          'qml-smoke-report.txt', 'ember.ini', 'credentials.json',
                          'id_rsa', 'id_ed25519') -or
            $lower.StartsWith('.env.') -or
            $lower -match '\.(sqlite|sqlite3|pem|key|p12|pfx)$') {
            throw 'Installer payload contains private data or credentials.'
        }
    }
}
if ($RequireVoiceBundle -and -not $RequireOfflineBundle) {
    throw '-RequireVoiceBundle requires -RequireOfflineBundle.'
}
if ($RequireOfflineBundle) {
    $manifest = Join-Path $payload 'offline-assets.json'
    if (!(Test-Path $manifest)) { throw 'Complete offline asset manifest is missing.' }
    $verifyArgs = @('verify', '--root', $payload, '--manifest', $manifest, '--redistribution-required')
    if ($RequireVoiceBundle) { $verifyArgs += '--require-voice' }
    & python (Join-Path $root 'tools/offline_bundle.py') @verifyArgs
    if ($LASTEXITCODE -ne 0) { throw 'Offline asset hash, TTS presence or license approval failed.' }
}

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
    throw 'Inno Setup 6 ISCC.exe not found. Install a trusted compiler manually before offline packaging; no automatic dependency downloads.'
}
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
    # RUNNER_TEMP is a GitHub Actions variable, not a Windows prerequisite.
    # Keep each local smoke install isolated and never overwrite user data.
    $scratch = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [System.IO.Path]::GetTempPath() }
    $install = Join-Path $scratch ('Ember-Installer-Smoke-' + [guid]::NewGuid().ToString('N'))
    if (Test-Path $install) { throw "Refusing to overwrite existing folder: $install" }
    $args = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', ('/DIR="' + $install + '"'))
    $setup = Start-Process -FilePath $installer -ArgumentList $args -PassThru -Wait
    if ($setup.ExitCode -ne 0) { throw "Silent install failed: $($setup.ExitCode)" }
    $installedExe = Join-Path $install 'Ember.exe'
    if (!(Test-Path $installedExe)) { throw 'Ember.exe missing after install.' }
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QUICK_BACKEND = 'software'
    $env:QT_OPENGL = 'software'
    $diagnostic = Join-Path $scratch ('Ember-Install-Smoke-' + [guid]::NewGuid().ToString('N') + '.txt')
    $env:EMBER_SMOKE_REPORT_PATH = $diagnostic
    try {
        $smoke = Start-Process -FilePath $installedExe -ArgumentList '--smoke-test' -PassThru -Wait
    } finally {
        Remove-Item Env:\EMBER_SMOKE_REPORT_PATH -ErrorAction SilentlyContinue
    }
    if (Test-Path $diagnostic) {
        Get-Content -LiteralPath $diagnostic
        Remove-Item -LiteralPath $diagnostic -Force
    }
    if ($smoke.ExitCode -ne 0) { throw "Installed app could not open: $($smoke.ExitCode)" }
    if (Test-Path (Join-Path $install 'data')) { throw 'Smoke launch wrote private SQLite/data to the installed application.' }
    if (Test-Path (Join-Path $install 'cache')) { throw 'Smoke launch wrote temporary microphone files into the installed application.' }
    if (Test-Path (Join-Path $install 'qml-smoke-report.txt')) { throw 'Smoke report leaked into installed folder.' }
    $uninstaller = Get-ChildItem -LiteralPath $install -Filter 'unins*.exe' | Select-Object -First 1
    if (!$uninstaller) { throw 'Uninstaller not found.' }
    $uninstall = Start-Process -FilePath $uninstaller.FullName -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -PassThru -Wait
    if ($uninstall.ExitCode -ne 0) { throw "Silent uninstall failed: $($uninstall.ExitCode)" }
    if (Test-Path $installedExe) { throw 'Uninstall left Ember.exe behind.' }
    Write-Host 'Per-user install, real QML launch, and uninstall smoke passed.'
}

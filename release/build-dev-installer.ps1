param(
    [string]$PayloadDirectory = 'build/Release',
    [string]$OutputDirectory = 'dist',
    [string]$Version = '0.1.0-dev',
    [switch]$VerifyInstall,
    [switch]$RequireOfflineBundle,
    [switch]$RequireVoiceBundle,
    [switch]$DiskSpanning
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
# A development runner may already have MSVC/Qt DLLs installed. Native
# portable users must receive every non-system PE import inside this folder.
# Analyze executable import tables without running any untrusted model engine.
& python (Join-Path $root 'tools/windows_pe_audit.py') --root $payload
if ($LASTEXITCODE -ne 0) {
    throw 'Native Windows x64 PE import dependency closure failed.'
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
$defines = @("/DAppPayload=$payload", "/DAppOutput=$output",
             "/DBuildVersion=$Version", "/DBuildNumericVersion=$numericVersion")
if ($DiskSpanning) { $defines += '/DAppDiskSpanning=1' }
& $compiler @defines $iss
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
$installerName = "Ember-$Version-Windows-x64.exe"
$installer = Join-Path $output $installerName
if (!(Test-Path $installer)) { throw "Installer missing: $installer" }
if ($DiskSpanning) {
    $slices = @(Get-ChildItem -LiteralPath $output -File |
        Where-Object { $_.Name -match ('^' + [regex]::Escape(
            "Ember-$Version-Windows-x64") + '-[1-9][0-9]*[.]bin$') })
    if ($slices.Count -eq 0) {
        throw 'Inno disk spanning produced no model data slices; incomplete installer.'
    }
    Write-Host ("Disk-spanned installer: {0} required local data slices." -f $slices.Count)
}
$hash = (Get-FileHash -Algorithm SHA256 -Path $installer).Hash.ToLowerInvariant()
$checksumName = if ($Version.EndsWith('-dev')) { 'Ember-dev-installer-SHA256.txt' } else { "Ember-$Version-SHA256.txt" }
Set-Content -Path (Join-Path $output $checksumName) -Value ($hash + '  ' + $installerName) -Encoding ascii
Write-Host "Unsigned development installer created. SHA256: $hash"

if ($VerifyInstall) {
    # A one-shot smoke install is isolated under a brand-new random temp path.
    # Always uninstall, even if the first boot/manifest verification fails.
    # Do not touch existing real user installations or the SQLite archive.
    $scratch = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [System.IO.Path]::GetTempPath() }
    $scratch = (Resolve-Path -LiteralPath $scratch -ErrorAction Stop).Path
    $install = Join-Path $scratch ('Ember-Installer-Smoke-' + [guid]::NewGuid().ToString('N'))
    if (Test-Path -LiteralPath $install) {
        throw "Refusing to overwrite existing smoke install: $install"
    }
    $installedExe = Join-Path $install 'Ember.exe'
    $diagnostic = Join-Path $scratch ('Ember-Install-Smoke-' + [guid]::NewGuid().ToString('N') + '.txt')
    $setupLaunched = $false
    $didUninstall = $false
    try {
        $args = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', ('/DIR="' + $install + '"'))
        $setupLaunched = $true
        $setup = Start-Process -FilePath $installer -ArgumentList $args -PassThru -Wait
        if ($setup.ExitCode -ne 0) { throw "Silent install failed: $($setup.ExitCode)" }
        if (!(Test-Path -LiteralPath $installedExe -PathType Leaf)) {
            throw 'Ember.exe missing after install.'
        }
        if ($RequireOfflineBundle) {
            # Rehash actual installed model, runtime and license bytes.
            $installedManifest = Join-Path $install 'offline-assets.json'
            if (!(Test-Path -LiteralPath $installedManifest -PathType Leaf)) {
                throw 'Offline asset inventory missing from INSTALLED directory.'
            }
            $installedVerifyArgs = @('verify', '--root', $install,
                '--manifest', $installedManifest, '--redistribution-required')
            if ($RequireVoiceBundle) { $installedVerifyArgs += '--require-voice' }
            & python (Join-Path $root 'tools/offline_bundle.py') @installedVerifyArgs
            if ($LASTEXITCODE -ne 0) {
                throw 'Installed LLM/RU-EN ASR/TTS or license failed integrity validation.'
            }
            Write-Host 'Installed offline model and engine inventory passed SHA-256 verification.'
        }
        # Qt's offscreen settings belong ONLY to the child smoke invocation.
        # Restore the caller's exact variables whether QML opens or fails.
        $savedQtEnvironment = @{}
        foreach ($variable in @('QT_QPA_PLATFORM', 'QT_QUICK_BACKEND',
                                'QT_OPENGL', 'EMBER_SMOKE_REPORT_PATH')) {
            $savedQtEnvironment[$variable] =
                [Environment]::GetEnvironmentVariable($variable, 'Process')
        }
        try {
            [Environment]::SetEnvironmentVariable('QT_QPA_PLATFORM', 'offscreen', 'Process')
            [Environment]::SetEnvironmentVariable('QT_QUICK_BACKEND', 'software', 'Process')
            [Environment]::SetEnvironmentVariable('QT_OPENGL', 'software', 'Process')
            [Environment]::SetEnvironmentVariable('EMBER_SMOKE_REPORT_PATH', $diagnostic, 'Process')
            $smoke = Start-Process -FilePath $installedExe -ArgumentList '--smoke-test' -PassThru -Wait
        } finally {
            foreach ($variable in $savedQtEnvironment.Keys) {
                [Environment]::SetEnvironmentVariable(
                    $variable, $savedQtEnvironment[$variable], 'Process')
            }
        }
        if ($smoke.ExitCode -ne 0) {
            throw "Installed Ember QML smoke failed: $($smoke.ExitCode)"
        }
        if (Test-Path -LiteralPath (Join-Path $install 'data')) {
            throw 'Smoke run unexpectedly wrote private SQLite/data into installed app.'
        }
        if (Test-Path -LiteralPath (Join-Path $install 'cache')) {
            throw 'Smoke run unexpectedly wrote microphone cache into installed app.'
        }
        if (Test-Path -LiteralPath (Join-Path $install 'qml-smoke-report.txt')) {
            throw 'Smoke report leaked into installed app.'
        }
        Write-Host 'Native Ember QML launch, offline asset inventory and privacy smoke passed.'
    } finally {
        # The smoke report goes only to a new temp file, never to packaged
        # application files, and is always removed even after an exception.
        if (Test-Path -LiteralPath $diagnostic -PathType Leaf) {
            Get-Content -LiteralPath $diagnostic
            Remove-Item -LiteralPath $diagnostic -Force -ErrorAction SilentlyContinue
        }
        if ($setupLaunched -and (Test-Path -LiteralPath $install -PathType Container)) {
            $uninstaller = Get-ChildItem -LiteralPath $install -Filter 'unins*.exe' -File |
                Select-Object -First 1
            if ($uninstaller) {
                $uninstall = Start-Process -FilePath $uninstaller.FullName -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -PassThru -Wait
                if ($uninstall.ExitCode -ne 0) {
                    throw "Test-only Ember uninstall failed: $($uninstall.ExitCode)"
                }
                $didUninstall = $true
            }
            # Never blindly recurse-delete this directory. If setup crashed,
            # preserve unexpected contents for manual fault analysis.
            if (Test-Path -LiteralPath $installedExe) {
                throw 'Test-only uninstaller failed or was missing; Ember.exe remains.'
            }
        }
    }
    if (!$didUninstall) {
        throw 'Smoke installer did not produce a complete reversible installation.'
    }
    Write-Host 'Per-user installation and rollback of test-only install passed.'
}

# User-initiated, all-in-one Ember offline Windows distribution assembly.
# No fetching, recurring jobs, monitoring, scheduled tasks, GitHub Actions or
# publishing. All EXEs, GGUFs, WAV models and reviewed license files must
# already be present in trusted local directories.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ApplicationDirectory,
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

if ($Version -notmatch '^([0-9]+)[.]([0-9]+)[.]([0-9]+)(-dev)?$') {
    throw 'Version must be MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-dev.'
}
$root = Split-Path -Parent $PSScriptRoot
$pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
if (-not $pythonCommand) { throw 'Python 3 is needed on the BUILD machine (not in the packaged app).' }
$python = $pythonCommand.Source
$innoCandidates = @(
    (Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Inno Setup 6\ISCC.exe'),
    (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'Inno Setup 6\ISCC.exe')
)
$inno = $innoCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $inno) {
    $innoCommand = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($innoCommand) { $inno = $innoCommand.Source }
}
if (-not $inno) {
    throw 'Inno Setup 6 ISCC.exe must be installed manually before compiling a Windows installer.'
}

function Must-BeRealDirectory([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label is missing: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not be a junction or symlink: $Path"
    }
    return $item.FullName.TrimEnd([char[]]@('\','/'))
}
function Run-CheckedPython([string[]]$Arguments) {
    & $python @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Offline build check failed (Python exit code $LASTEXITCODE)."
    }
}
function Under-Directory([string]$Candidate, [string]$Parent) {
    $prefix = $Parent.TrimEnd([char[]]@('\','/')) +
              [System.IO.Path]::DirectorySeparatorChar
    return $Candidate.StartsWith($prefix,
        [System.StringComparison]::OrdinalIgnoreCase)
}

$app = Must-BeRealDirectory $ApplicationDirectory 'Clean deployed Qt app'
$assets = Must-BeRealDirectory $AssetsDirectory 'Manually collected offline assets'
if (-not (Test-Path -LiteralPath $ApprovedManifest -PathType Leaf)) {
    throw 'Approved asset SHA-256/license manifest is missing.'
}
$manifestFile = Get-Item -LiteralPath $ApprovedManifest -Force
if (($manifestFile.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'Asset manifest must not be a symlink.'
}
$manifest = $manifestFile.FullName
$target = [System.IO.Path]::GetFullPath($DistributionDirectory).TrimEnd([char[]]@('\','/'))
if (Test-Path -LiteralPath $target) {
    throw 'Refusing to overwrite an existing distribution directory.'
}
$parentPath = Split-Path -Parent $target
$parent = Must-BeRealDirectory $parentPath 'Distribution parent'
if ((Under-Directory $target $app) -or (Under-Directory $target $assets) -or
    (Under-Directory $target $root) -or
    (Under-Directory $app $target) -or (Under-Directory $assets $target) -or
    ($target -ieq $app) -or ($target -ieq $assets) -or ($target -ieq $root)) {
    throw 'Distribution must be outside the repository, app and asset folders.'
}
# Verify the complete pair of bilingual dictation references before copying
# multi-gigabyte assets. These are private local test inputs, not part of the
# published installer or ZIP. A missing sample must not mean ASR "passed".
$asrInputs = @($EnglishWav, $EnglishReference, $RussianWav, $RussianReference)
$asrProvided = @($asrInputs | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }).Count
if ($asrProvided -ne 0 -and $asrProvided -ne 4) {
    throw 'Pass all four English/Russian WAV + reference inputs, or none.'
}
$hasAsrSamples = ($asrProvided -eq 4)
if ($hasAsrSamples) {
    foreach ($sample in $asrInputs) {
        if (-not (Test-Path -LiteralPath $sample -PathType Leaf)) {
            throw "Real local ASR sample/reference missing: $sample"
        }
        $item = Get-Item -LiteralPath $sample -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'ASR acceptance inputs must not be junctions or symlinks.'
        }
    }
}
# A stable edition must prove RU/EN dictation using real samples. The
# acceptance.json quality evidence is checked separately; this invocation
# adds a repeatable sample-level gate without inventing sample content.
if (-not $Version.EndsWith('-dev') -and -not $hasAsrSamples) {
    throw 'Stable release requires English and Russian WAV + reference text for actual ASR.'
}
# Guard the release path before copies of multi-gigabyte GGUF files are made.
# A production SemVer build requires the EXISTING independently approved
# release record. A development candidate is not silently promoted to stable.
if (-not $Version.EndsWith('-dev')) {
    if ($SkipLiveEngineChecks) {
        throw 'Stable release cannot skip real offline LLM and English speech inference.'
    }
    Run-CheckedPython -Arguments @((Join-Path $root 'release/verify_release.py'),
        '--tag', ('v' + $Version))
}
$offline = Join-Path $root 'tools/offline_bundle.py'
Run-CheckedPython -Arguments @($offline, 'verify', '--root', $assets, '--manifest',
    $manifest, '--redistribution-required', '--require-voice')

# Never leave a partially assembled release directory at the final path.
# This transaction creates a uniquely named private staging folder on the
# SAME volume as the final destination and moves it only after both outputs
# and their hashes are verified.
$work = Join-Path $parent ('.ember-build-' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $work 'payload'
$publish = Join-Path $work 'distribution'
$committed = $false
try {
    New-Item -ItemType Directory -Path $work -ErrorAction Stop | Out-Null
    New-Item -ItemType Directory -Path $publish -ErrorAction Stop | Out-Null
    Run-CheckedPython -Arguments @($offline, 'assemble', '--root', $assets,
        '--manifest', $manifest, '--application', $app,
        '--output', $payload, '--require-voice')
    # Check every executable/DLL import before producing a downloadable file.
    Run-CheckedPython -Arguments @((Join-Path $root 'tools/windows_pe_audit.py'),
        '--root', $payload)
    # This is a FOREGROUND, user-invoked packaging command, not a recurring
    # background monitor. Real engine checks launch trusted bundled workers
    # once, verify the generated text/PCM and stop them before packaging.
    # Development candidates may opt out explicitly, but stable may not.
    if (-not $SkipLiveEngineChecks) {
        $textArgs = @((Join-Path $root 'tools/offline_acceptance.py'),
            '--root', $payload, '--run', '--gpu-layers', '99',
            '--context', '8192')
        if ($hasAsrSamples) {
            $textArgs += @('--en-wav', $EnglishWav,
                '--en-reference', $EnglishReference,
                '--ru-wav', $RussianWav,
                '--ru-reference', $RussianReference,
                '--max-wer', $MaxAsrWer.ToString(
                    [System.Globalization.CultureInfo]::InvariantCulture))
        } else {
            Write-Warning 'DEV ONLY: real RU/EN transcription has NOT been tested.'
        }
        Run-CheckedPython -Arguments $textArgs
        $voiceModel = if (Test-Path -LiteralPath
            (Join-Path $payload 'models/speech/qwen3-tts-1.7b.gguf')) {
            'qwen3-tts-1.7b'
        } else {
            'qwen3-tts-0.6b'
        }
        Run-CheckedPython -Arguments @((Join-Path $root 'tools/native_speech_acceptance.py'),
            '--root', $payload, '--run', '--model', $voiceModel,
            '--device', 'auto', '--samples', '2')
    } else {
        Write-Warning 'DEV ONLY: real LLM/TTS and RU/EN ASR inference was skipped. Candidate is NOT acceptance-tested.'
    }
    $zipName = "Ember-$Version-Windows-x64-portable.zip"
    $zipPath = Join-Path $publish $zipName
    Run-CheckedPython -Arguments @((Join-Path $root 'tools/portable_zip.py'),
        '--root', $payload, '--output', $zipPath, '--require-voice')
    # A full 9B-class GGUF plus speech weights normally exceeds Windows'
    # ~4 GB single-EXE ceiling. Inno creates a small Setup.exe and numbered
    # model-data slices; the user receives those as ONE verified ZIP64 download.
    & (Join-Path $root 'release/build-dev-installer.ps1') `
        -PayloadDirectory $payload `
        -OutputDirectory $publish `
        -Version $Version `
        -RequireOfflineBundle `
        -RequireVoiceBundle `
        -DiskSpanning `
        -VerifyInstall
    if (-not $?) { throw 'Inno Setup installation/uninstallation verification failed.' }
    $installerName = "Ember-$Version-Windows-x64.exe"
    $installerPath = Join-Path $publish $installerName
    if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $zipPath -PathType Leaf)) {
        throw 'Expected offline installer executable or portable ZIP is missing.'
    }
    $bundleName = "Ember-$Version-Windows-x64-installer-bundle.zip"
    $bundlePath = Join-Path $publish $bundleName
    Run-CheckedPython -Arguments @((Join-Path $root 'tools/installer_bundle.py'),
        '--directory', $publish, '--version', $Version, '--output', $bundlePath)
    if (-not (Test-Path -LiteralPath $bundlePath -PathType Leaf)) {
        throw 'Validated complete installer bundle is missing.'
    }
    $bundleHash = (Get-FileHash -LiteralPath $bundlePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksums = @("$bundleHash  $bundleName", "$zipHash  $zipName") -join "`n"
    [System.IO.File]::WriteAllText(
        (Join-Path $publish 'SHA256SUMS.txt'), $checksums + "`n",
        (New-Object System.Text.UTF8Encoding($false)))
    # The validated installer download now contains the EXE and every
    # numbered .bin slice. Remove the WORKING copies only from our private
    # staging directory, avoiding duplicate multi-GB downloads. The existing
    # standalone installer checksum references the removed raw EXE; only
    # SHA256SUMS.txt is distributed.
    $sliceRx = '^' + [regex]::Escape("Ember-$Version-Windows-x64") +
               '-[1-9][0-9]*[.]bin
    Write-Host 'Packaging/smoke success is NOT real model/voice/GPU acceptance.'
} finally {
    # This folder was created by this invocation with a fresh GUID; never
    # delete an unrelated user path or a prior completed distribution.
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
if (-not $committed) { throw 'Offline distribution was not completed.' }

    foreach ($slice in @(Get-ChildItem -LiteralPath $publish -File |
            Where-Object { $_.Name -match $sliceRx })) {
        Remove-Item -LiteralPath $slice.FullName -Force
    }
    Remove-Item -LiteralPath $installerPath -Force
    $oldChecksum = if ($Version.EndsWith('-dev')) {
        'Ember-dev-installer-SHA256.txt'
    } else {
        "Ember-$Version-SHA256.txt"
    }
    $oldChecksumPath = Join-Path $publish $oldChecksum
    if (Test-Path -LiteralPath $oldChecksumPath -PathType Leaf) {
        Remove-Item -LiteralPath $oldChecksumPath -Force
    }
    # Keep two complete, independently downloadable distributions (installer
    # bundle ZIP and portable ZIP) plus checksums; no extra model copy.
    Remove-Item -LiteralPath $payload -Recurse -Force
    Move-Item -LiteralPath $publish -Destination $target -ErrorAction Stop
    $committed = $true
    Write-Host "Offline Ember package built locally: $target"
    Write-Host 'Contains complete installer-bundle ZIP, portable ZIP and SHA256SUMS.txt.'
    Write-Host 'Packaging/smoke success is NOT real model/voice/GPU acceptance.'
} finally {
    # This folder was created by this invocation with a fresh GUID; never
    # delete an unrelated user path or a prior completed distribution.
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
if (-not $committed) { throw 'Offline distribution was not completed.' }

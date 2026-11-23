# Explicit, one-shot app-local MSVC x64 runtime staging for Qt/Ember.
# Never downloads binaries, runs native EXEs, changes PATH/registry or creates tasks.
# Microsoft Visual C++ redistribution rights require the builder to review
# their own licensed Visual Studio Software License Terms (not granted here).
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PayloadDirectory,
    [switch]$PruneDevelopmentTools
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$payloadItem = Get-Item -LiteralPath $PayloadDirectory -Force -ErrorAction Stop
if (-not $payloadItem.PSIsContainer -or
    ($payloadItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'App-local payload must be a real directory, not a link.'
}
$payload = $payloadItem.FullName
if (!(Test-Path -LiteralPath (Join-Path $payload 'Ember.exe') -PathType Leaf)) {
    throw 'App-local staging requires an existing native Ember.exe.'
}

# Search only the documented local VS 2022 CRT redistributable directories.
# Never take a convenient vcruntime from the developer machine's PATH or
# System32, which could be an unrelated version or unapproved binary.
$vsRoot = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'Microsoft Visual Studio\2022'
if (!(Test-Path -LiteralPath $vsRoot -PathType Container)) {
    throw 'Visual Studio 2022 redistributable source directory was not found.'
}
$runtimeFiles = @(
    'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
    'vcruntime140.dll', 'vcruntime140_1.dll'
)
$candidates = @()
foreach ($edition in @(Get-ChildItem -LiteralPath $vsRoot -Directory -ErrorAction Stop)) {
    if (($edition.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
    $redistRoot = Join-Path $edition.FullName 'VC\Redist\MSVC'
    if (!(Test-Path -LiteralPath $redistRoot -PathType Container)) { continue }
    foreach ($version in @(Get-ChildItem -LiteralPath $redistRoot -Directory)) {
        if (($version.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
        $folder = Join-Path $version.FullName 'x64\Microsoft.VC143.CRT'
        if (!(Test-Path -LiteralPath $folder -PathType Container)) { continue }
        $item = Get-Item -LiteralPath $folder -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
        $complete = $true
        foreach ($name in $runtimeFiles) {
            if (!(Test-Path -LiteralPath (Join-Path $folder $name) -PathType Leaf)) {
                $complete = $false
                break
            }
        }
        if ($complete) {
            $parsedVersion = $null
            if ([version]::TryParse($version.Name, [ref]$parsedVersion)) {
                $candidates += [pscustomobject]@{
                    Version = $parsedVersion; Folder = $folder
                }
            }
        }
    }
}
$source = $candidates | Sort-Object -Property Version -Descending |
    Select-Object -First 1
if ($null -eq $source) {
    throw 'No complete official VS 2022 x64 Microsoft.VC143.CRT redistributable set found.'
}

$copied = 0
foreach ($name in $runtimeFiles) {
    $origin = Join-Path $source.Folder $name
    $item = Get-Item -LiteralPath $origin -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Official CRT source must not be a symlink or junction.'
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $origin
    if ($signature.Status -ne 'Valid' -or
        $null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch 'Microsoft') {
        throw "CRT file lacks a valid Microsoft Authenticode signature: $name"
    }
    $destination = Join-Path $payload $name
    $sha = (Get-FileHash -LiteralPath $origin -Algorithm SHA256).Hash
    if (Test-Path -LiteralPath $destination) {
        $existing = Get-Item -LiteralPath $destination -Force
        if (($existing.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0 -or
            $existing.PSIsContainer -or
            (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $sha) {
            throw "Existing app-local CRT conflicts with the verified MSVC runtime: $name"
        }
        continue
    }
    Copy-Item -LiteralPath $origin -Destination $destination -ErrorAction Stop
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $sha) {
        Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
        throw "CRT file changed while staging: $name"
    }
    $copied++
}

if ($PruneDevelopmentTools) {
    # A Release build can still retain PDB / static link artifacts. Remove
    # these ONLY from the selected freshly deployed application payload, not
    # from the source checkout, SDK or private build databases.
    foreach ($symbol in @(Get-ChildItem -LiteralPath $payload -Recurse -File -Force)) {
        if ($symbol.Extension.ToLowerInvariant() -in @('.pdb','.ilk','.exp','.obj','.lib')) {
            if (($symbol.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Developer symbol path is linked; refusing to follow it.'
            }
            Remove-Item -LiteralPath $symbol.FullName -Force
        }
    }
    # These EXEs are build-time tools. Bundling them pulls in Qt6Test.dll and
    # unnecessary libraries (and exposes test/debug functions to users).
    foreach ($name in @('ember-core-tests.exe', 'ember-icon-builder.exe', 'Qt6Test.dll')) {
        $file = Join-Path $payload $name
        if (Test-Path -LiteralPath $file -PathType Leaf) {
            $node = Get-Item -LiteralPath $file -Force
            if (($node.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Development-only binary is linked; refusing to delete.'
            }
            Remove-Item -LiteralPath $file -Force
        }
    }
    # Ember uses Qt SQL only for local SQLite. Third-party Mimer/PostgreSQL
    # clients are not packaged or installed: remove their unused Qt plugins
    # instead of demanding proprietary runtime DLLs on an offline computer.
    $sql = Join-Path $payload 'sqldrivers'
    if (Test-Path -LiteralPath $sql -PathType Container) {
        $folder = Get-Item -LiteralPath $sql -Force
        if (($folder.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'SQL plugin directory is linked; refusing changes.'
        }
        foreach ($plugin in @(Get-ChildItem -LiteralPath $sql -File -Filter '*.dll')) {
            if ($plugin.Name -ieq 'qsqlite.dll') { continue }
            if (($plugin.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'SQL plugin is a link; refusing changes.'
            }
            Remove-Item -LiteralPath $plugin.FullName -Force
        }
        if (!(Test-Path -LiteralPath (Join-Path $sql 'qsqlite.dll') -PathType Leaf)) {
            throw 'SQLite Qt plugin is missing after development tool pruning.'
        }
    }
}
Write-Host ("Official Microsoft-signed MSVC app-local runtime present: {0} DLLs; newly copied {1}." -f $runtimeFiles.Count, $copied)
Write-Host 'All copying was local and bounded. Redistribution/license approval not inferred.'

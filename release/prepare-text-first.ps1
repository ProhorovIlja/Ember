# Offline manual staging, Windows PowerShell 5.1 compatible.
# No downloaded packages, processes, networking, or scheduled tasks.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$AppDirectory,
    [string]$LlamaServer = '',
    [string]$ModelGguf = '',
    [string]$WhisperCli = '',
    [string]$WhisperModel = '',
    [switch]$RequireAsr,
    [switch]$CheckOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function File-Check([string]$Path, [string]$Description) {
    if ([string]::IsNullOrWhiteSpace($Path) -or
        -not [System.IO.File]::Exists($Path)) {
        throw "$Description not found: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description is a symlink: $Path"
    }
    return $item
}
function Header-Check([string]$Path, [byte[]]$Magic, [string]$Description) {
    $item = File-Check $Path $Description
    if ($item.Length -lt 128) { throw "$Description is too small: $Path" }
    $stream = [System.IO.File]::OpenRead($item.FullName)
    try {
        foreach ($expected in $Magic) {
            if ($stream.ReadByte() -ne [int]$expected) {
                throw "$Description has unexpected file magic: $Path"
            }
        }
    } finally { $stream.Dispose() }
    return $item
}
function Assert-PeX64([string]$Path, [string]$Description, [string]$Extension = '.exe') {
    $item = Header-Check $Path ([byte[]](77,90)) $Description
    if ($item.Extension -ine $Extension -or $item.Length -lt 256) { throw "$Description must be a Windows x64 $Extension file." }
    $fs = [System.IO.File]::OpenRead($item.FullName)
    try {
        $r = New-Object System.IO.BinaryReader($fs)
        $dos = $r.ReadBytes(64)
        if ($dos.Length -ne 64) { throw "$Description has a short DOS header." }
        $offset = [System.BitConverter]::ToInt32($dos,60)
        if ($offset -lt 64 -or $offset -gt 4096 -or ([long]$offset + 6) -gt $item.Length) {
            throw "$Description has an invalid PE header offset."
        }
        $fs.Position = $offset
        $h = $r.ReadBytes(6)
        if ($h.Length -ne 6 -or $h[0] -ne 80 -or $h[1] -ne 69 -or $h[2] -ne 0 -or
            $h[3] -ne 0 -or [System.BitConverter]::ToUInt16($h,4) -ne 0x8664) {
            throw "$Description is not Windows x64/AMD64 PE."
        }
    } finally { $fs.Dispose() }
    return $item
}
function Assert-Gguf([string]$Path, [string]$Description) {
    $item = Header-Check $Path ([byte[]](71,71,85,70)) $Description
    if ($item.Extension -ine '.gguf' -or $item.Length -lt 1MB) { throw "$Description needs GGUF >=1 MiB." }
    $fs = [System.IO.File]::OpenRead($item.FullName)
    try {
        $fs.Position = 4
        $v = New-Object byte[] 4
        if ($fs.Read($v,0,4) -ne 4 -or
            [System.BitConverter]::ToUInt32($v,0) -notin @(2,3)) { throw "$Description must be GGUF v2/v3." }
    } finally { $fs.Dispose() }
    return $item
}
function Assert-MultilingualWhisper([string]$Path, [string]$Description) {
    # whisper.cpp GGML model: little-endian magic 0x67676d6c.
    $item = Header-Check $Path ([byte[]](108,109,103,103)) $Description
    if ($item.Extension -ine '.bin' -or $item.Length -lt 1MB -or
        $item.Name -match '\.en\.bin$') {
        throw "$Description needs multilingual whisper.cpp GGML .bin >=1 MiB (not .en.bin)."
    }
    return $item
}

function Same-Hash([string]$First, [string]$Second) {
    return (Get-FileHash -LiteralPath $First -Algorithm SHA256).Hash -eq
           (Get-FileHash -LiteralPath $Second -Algorithm SHA256).Hash
}
# Reject directory junctions/symlinks at EVERY existing destination ancestor,
# not only the chosen AppDirectory. Otherwise Move-Item can stage engine
# executables/models outside the user's selected payload through a nested link.
function Assert-SafeStageTarget([string]$Base, [string]$Destination) {
    $basePath = [System.IO.Path]::GetFullPath($Base).TrimEnd([char[]]@('\','/'))
    $targetPath = [System.IO.Path]::GetFullPath($Destination)
    $prefix = $basePath + [System.IO.Path]::DirectorySeparatorChar
    if (-not $targetPath.StartsWith(
            $prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Destination escapes the selected application: $Destination"
    }
    $parent = Split-Path -Parent $targetPath
    while ($parent.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $parent) {
            $node = Get-Item -LiteralPath $parent -Force
            if ((($node.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) -or
                -not $node.PSIsContainer) {
                throw "Destination parent is a link, junction or file: $parent"
            }
        }
        $parent = Split-Path -Parent $parent
    }
}
function Assert-X64CompanionDlls([string]$Directory) {
    $dlls = @(Get-ChildItem -LiteralPath $Directory -File -Filter '*.dll')
    if ($dlls.Count -gt 256) { throw "Too many runtime DLLs in $Directory." }
    foreach ($dll in $dlls) {
        [void](Assert-PeX64 $dll.FullName "Runtime DLL" '.dll')
    }
}
function Stage-File([string]$Source, [string]$Destination,
                    [System.Collections.Generic.List[string]]$Created) {
    Assert-SafeStageTarget $root $Destination
    $item = File-Check $Source 'Source file'
    if (Test-Path -LiteralPath $Destination) {
        [void](File-Check $Destination 'Existing target')
        if (-not (Same-Hash $item.FullName $Destination)) {
            throw "Refusing to replace a different user file: $Destination"
        }
        return
    }
    $folder = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $folder)) {
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
    }
    Assert-SafeStageTarget $root $Destination
    $partial = "$Destination.partial-$([guid]::NewGuid().ToString('N'))"
    try {
        Copy-Item -LiteralPath $item.FullName -Destination $partial
        if (-not (Same-Hash $item.FullName $partial)) {
            throw "SHA-256 mismatch while staging: $($item.Name)"
        }
        Move-Item -LiteralPath $partial -Destination $Destination
        $Created.Add($Destination)
    } finally {
        if (Test-Path -LiteralPath $partial) {
            Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
        }
    }
}
if (-not (Test-Path -LiteralPath $AppDirectory -PathType Container)) {
    throw "Deploy the native Qt app before staging: $AppDirectory"
}
$root = (Resolve-Path -LiteralPath $AppDirectory).Path
$rootItem = Get-Item -LiteralPath $root -Force
if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'AppDirectory cannot be a symlink or junction.'
}
[void](File-Check (Join-Path $root 'Ember.exe') 'Native application')
[void](File-Check (Join-Path $root 'Qt6Core.dll') 'Qt runtime')
$exeTarget = Join-Path $root 'engines\llama\llama-server.exe'
$modelTarget = Join-Path $root 'models\llm\model.gguf'
$asrTarget = Join-Path $root 'engines\whisper\whisper-cli.exe'
$asrModelTarget = Join-Path $root 'models\stt\ggml-base.bin'

if ($CheckOnly) {
    foreach ($target in @($exeTarget,$modelTarget)) {
        Assert-SafeStageTarget $root $target
    }
    [void](Assert-PeX64 $exeTarget 'Staged llama-server.exe')
    [void](Assert-Gguf $modelTarget 'Staged GGUF model')
    Assert-X64CompanionDlls (Split-Path -Parent $exeTarget)
    if ($RequireAsr) {
        foreach ($target in @($asrTarget,$asrModelTarget)) {
            Assert-SafeStageTarget $root $target
        }
        [void](Assert-PeX64 $asrTarget 'Staged whisper-cli.exe')
        [void](Assert-MultilingualWhisper $asrModelTarget 'Staged Whisper multilingual model')
        Assert-X64CompanionDlls (Split-Path -Parent $asrTarget)
    }
    Write-Host 'Offline text assets present; ASR was checked only if requested. No engine was executed.'
    Write-Host 'Check does not prove model format compatibility or actual inference.'
    return
}
if (-not $LlamaServer -or -not $ModelGguf) {
    throw 'Provide -LlamaServer and -ModelGguf, or use -CheckOnly.'
}
$engine = Assert-PeX64 $LlamaServer 'Windows llama-server'
$model = Assert-Gguf $ModelGguf 'GGUF model'
if ([bool]$WhisperCli -ne [bool]$WhisperModel) {
    throw 'Supply both -WhisperCli and -WhisperModel for RU/EN dictation.'
}
if ($RequireAsr -and -not $WhisperCli) {
    throw '-RequireAsr needs explicit -WhisperCli and -WhisperModel paths.'
}
$asrExe = $null
$asrModel = $null
if ($WhisperCli) {
    $asrExe = Assert-PeX64 $WhisperCli 'Whisper Windows executable'
    $asrModel = Assert-MultilingualWhisper $WhisperModel 'Multilingual Whisper model'
    
}

$companions = @(Get-ChildItem -LiteralPath $engine.DirectoryName -File -Filter '*.dll')
if ($companions.Count -gt 256) { throw 'Too many engine companion DLLs.' }
foreach ($dll in $companions) {
    [void](Assert-PeX64 $dll.FullName 'LLM runtime DLL' '.dll')
}
$asrCompanions = @()
if ($asrExe) {
    $asrCompanions = @(Get-ChildItem -LiteralPath $asrExe.DirectoryName -File -Filter '*.dll')
    if ($asrCompanions.Count -gt 256) { throw 'Too many ASR runtime DLLs.' }
    foreach ($dll in $asrCompanions) {
        [void](Assert-PeX64 $dll.FullName 'ASR runtime DLL' '.dll')
    }
}
$created = New-Object 'System.Collections.Generic.List[string]'
try {
    Stage-File $engine.FullName $exeTarget $created
    foreach ($dll in $companions) {
        Stage-File $dll.FullName (Join-Path (Split-Path -Parent $exeTarget) $dll.Name) $created
    }
    Stage-File $model.FullName $modelTarget $created
    if ($asrExe) {
        Stage-File $asrExe.FullName $asrTarget $created
        foreach ($dll in $asrCompanions) {
            Stage-File $dll.FullName (Join-Path (Split-Path -Parent $asrTarget) $dll.Name) $created
        }
        Stage-File $asrModel.FullName $asrModelTarget $created
    }
    [void](Assert-PeX64 $exeTarget 'Staged llama-server')
    [void](Assert-Gguf $modelTarget 'Staged GGUF')
    if ($asrExe) {
        [void](Assert-PeX64 $asrTarget 'Staged whisper-cli')
        $writtenModel = Assert-MultilingualWhisper $asrModelTarget 'Staged multilingual ASR model'
        
    }
} catch {
    foreach ($createdPath in $created) {
        Remove-Item -LiteralPath $createdPath -Force -ErrorAction SilentlyContinue
    }
    throw
}
Write-Host "Offline LLM assets prepared ($($created.Count) new files)."
Write-Host 'Optional multilingual ASR staged when both paths provided. No downloads or processes launched.'
Write-Host 'Open Ember.exe and press Start brain for text; dictation needs a real ASR model.'

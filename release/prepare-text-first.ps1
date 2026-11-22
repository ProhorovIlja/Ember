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
function Same-Hash([string]$First, [string]$Second) {
    return (Get-FileHash -LiteralPath $First -Algorithm SHA256).Hash -eq
           (Get-FileHash -LiteralPath $Second -Algorithm SHA256).Hash
}
function Stage-File([string]$Source, [string]$Destination,
                    [System.Collections.Generic.List[string]]$Created) {
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
    [void](Header-Check $exeTarget ([byte[]](77,90)) 'Staged llama-server.exe')
    [void](Header-Check $modelTarget ([byte[]](71,71,85,70)) 'Staged GGUF model')
    if ($RequireAsr) {
        [void](Header-Check $asrTarget ([byte[]](77,90)) 'Staged whisper-cli.exe')
        $asrItem = File-Check $asrModelTarget 'Staged Whisper multilingual model'
        if ($asrItem.Length -lt 128) { throw 'Staged multilingual ASR model is too small.' }
    }
    Write-Host 'Offline text assets present; ASR was checked only if requested. No engine was executed.'
    Write-Host 'Check does not prove model format compatibility or actual inference.'
    return
}
if (-not $LlamaServer -or -not $ModelGguf) {
    throw 'Provide -LlamaServer and -ModelGguf, or use -CheckOnly.'
}
$engine = Header-Check $LlamaServer ([byte[]](77,90)) 'Windows llama-server'
$model = Header-Check $ModelGguf ([byte[]](71,71,85,70)) 'GGUF model'
if ([bool]$WhisperCli -ne [bool]$WhisperModel) {
    throw 'Supply both -WhisperCli and -WhisperModel for RU/EN dictation.'
}
if ($RequireAsr -and -not $WhisperCli) {
    throw '-RequireAsr needs explicit -WhisperCli and -WhisperModel paths.'
}
$asrExe = $null
$asrModel = $null
if ($WhisperCli) {
    $asrExe = Header-Check $WhisperCli ([byte[]](77,90)) 'Whisper Windows executable'
    $asrModel = File-Check $WhisperModel 'Multilingual Whisper model'
    if ($asrExe.Extension -ine '.exe' -or $asrModel.Extension -ine '.bin' -or
        $asrModel.Length -lt 128) {
        throw 'Expected whisper-cli.exe and a nonempty multilingual GGML .bin model.'
    }
}
if ($engine.Extension -ine '.exe' -or $model.Extension -ine '.gguf') {
    throw 'Expected a Windows .exe engine and .gguf model.'
}
$companions = @(Get-ChildItem -LiteralPath $engine.DirectoryName -File -Filter '*.dll')
if ($companions.Count -gt 256) { throw 'Too many engine companion DLLs.' }
foreach ($dll in $companions) {
    if (($dll.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Symlinked engine DLL not accepted: $($dll.FullName)"
    }
}
$asrCompanions = @()
if ($asrExe) {
    $asrCompanions = @(Get-ChildItem -LiteralPath $asrExe.DirectoryName -File -Filter '*.dll')
    if ($asrCompanions.Count -gt 256) { throw 'Too many ASR runtime DLLs.' }
    foreach ($dll in $asrCompanions) {
        if (($dll.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Symlinked ASR DLL not accepted: $($dll.FullName)"
        }
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
    [void](Header-Check $exeTarget ([byte[]](77,90)) 'Staged llama-server')
    [void](Header-Check $modelTarget ([byte[]](71,71,85,70)) 'Staged GGUF')
    if ($asrExe) {
        [void](Header-Check $asrTarget ([byte[]](77,90)) 'Staged whisper-cli')
        $writtenModel = File-Check $asrModelTarget 'Staged multilingual ASR model'
        if ($writtenModel.Length -lt 128) { throw 'Staged ASR model is too small.' }
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

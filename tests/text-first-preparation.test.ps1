Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Join-Path ([System.IO.Path]::GetTempPath()) ('ember-offline-text-' + [guid]::NewGuid().ToString('N'))
$prepare = Join-Path (Split-Path -Parent $PSScriptRoot) 'release/prepare-text-first.ps1'
function Mock-File([string]$Path, [byte[]]$Magic) {
    $raw = New-Object byte[] 256
    [array]::Copy($Magic, $raw, $Magic.Length)
    [System.IO.File]::WriteAllBytes($Path, $raw)
}

function Mock-Exe([string]$Path, [int]$Machine = 0x8664) {
    $raw = New-Object byte[] 512
    $raw[0] = 77; $raw[1] = 90; $raw[60] = 128
    $raw[128] = 80; $raw[129] = 69
    $raw[132] = [byte]($Machine -band 255)
    $raw[133] = [byte](($Machine -shr 8) -band 255)
    [System.IO.File]::WriteAllBytes($Path, $raw)
}
function Mock-Gguf([string]$Path, [int]$Version = 3) {
    $raw = New-Object byte[] (1MB + 128)
    $raw[0] = 71; $raw[1] = 71; $raw[2] = 85; $raw[3] = 70
    [array]::Copy([System.BitConverter]::GetBytes([uint32]$Version), 0, $raw, 4, 4)
    [System.IO.File]::WriteAllBytes($Path, $raw)
}
function Mock-Whisper([string]$Path) {
    $raw = New-Object byte[] (1MB + 128)
    $raw[0] = 108; $raw[1] = 109; $raw[2] = 103; $raw[3] = 103
    [System.IO.File]::WriteAllBytes($Path, $raw)
}

try {
    $app = Join-Path $root 'app'
    $eng = Join-Path $root 'engine'
    $weights = Join-Path $root 'weights'
    foreach ($d in @($app,$eng,$weights)) {
        New-Item -ItemType Directory -Force -Path $d | Out-Null
    }
    Mock-File (Join-Path $app 'Ember.exe') ([byte[]](77,90))
    Mock-File (Join-Path $app 'Qt6Core.dll') ([byte[]](77,90))
    Mock-Exe (Join-Path $eng 'llama-server.exe')
    Mock-Exe (Join-Path $eng 'ggml-base.dll')
    Mock-Gguf (Join-Path $weights 'local.gguf')
    Mock-Exe (Join-Path $eng 'whisper-cli.exe')
    Mock-Whisper (Join-Path $weights 'ggml-base.bin')
    $arguments = @{
        AppDirectory = $app
        LlamaServer = (Join-Path $eng 'llama-server.exe')
        ModelGguf = (Join-Path $weights 'local.gguf')
        WhisperCli = (Join-Path $eng 'whisper-cli.exe')
        WhisperModel = (Join-Path $weights 'ggml-base.bin')
    }
    & $prepare @arguments
    & $prepare -AppDirectory $app -CheckOnly -RequireAsr
    & $prepare @arguments
    if (-not (Test-Path (Join-Path $app 'engines/whisper/whisper-cli.exe')) -or
        -not (Test-Path (Join-Path $app 'models/stt/ggml-base.bin'))) {
        throw 'Multilingual dictation engine/model were not staged.'
    }
    $withoutAsr = @{
        AppDirectory = $app
        LlamaServer = (Join-Path $eng 'llama-server.exe')
        ModelGguf = (Join-Path $weights 'local.gguf')
        RequireAsr = $true
    }
    $blocked = $false
    try { & $prepare @withoutAsr | Out-Null } catch { $blocked = $true }
    if (-not $blocked) { throw 'Missing RU/EN ASR dependency was silently accepted.' }
    if (-not (Test-Path (Join-Path $app 'engines/llama/ggml-base.dll'))) {
        throw 'Required sibling engine DLL was not staged.'
    }
    $destination = Join-Path $app 'models/llm/model.gguf'
    $before = (Get-FileHash $destination -Algorithm SHA256).Hash
    $alternate = Join-Path $weights 'different.gguf'
    Mock-Gguf $alternate
    $raw = [System.IO.File]::ReadAllBytes($alternate)
    $raw[42] = 59
    [System.IO.File]::WriteAllBytes($alternate, $raw)
    $arguments.ModelGguf = $alternate
    $blocked = $false
    try { & $prepare @arguments | Out-Null } catch { $blocked = $true }
    if (-not $blocked) { throw 'Clobbering existing model was permitted.' }
    if ((Get-FileHash $destination -Algorithm SHA256).Hash -ne $before) {
        throw 'Rejected overwrite damaged the existing GGUF.'
    }
    $invalid = Join-Path $weights 'invalid.gguf'
    Mock-File $invalid ([byte[]](65,66,67,68))
    $arguments.ModelGguf = $invalid
    $blocked = $false
    try { & $prepare @arguments | Out-Null } catch { $blocked = $true }
    if (-not $blocked) { throw 'Invalid GGUF file magic was accepted.' }

    # Wrong Windows machine, unsupported GGUF version and invalid Whisper model
    # must fail before any staged files are replaced.
    $wrongMachine = Join-Path $eng 'x86.exe'
    Mock-Exe $wrongMachine 0x14c
    $arguments.LlamaServer = $wrongMachine
    $blocked = $false
    try { & $prepare @arguments | Out-Null } catch { $blocked = $true }
    if (-not $blocked) { throw '32-bit Windows engine passed preflight.' }
    $arguments.LlamaServer = Join-Path $eng 'llama-server.exe'

    $wrongVersion = Join-Path $weights 'v99.gguf'
    Mock-Gguf $wrongVersion 99
    $arguments.ModelGguf = $wrongVersion
    $blocked = $false
    try { & $prepare @arguments | Out-Null } catch { $blocked = $true }
    if (-not $blocked) { throw 'Unsupported GGUF version passed preflight.' }
    $arguments.ModelGguf = Join-Path $weights 'local.gguf'

    $englishOnly = Join-Path $weights 'ggml-base.en.bin'
    Mock-Whisper $englishOnly
    $arguments.WhisperModel = $englishOnly
    $blocked = $false
    try { & $prepare @arguments | Out-Null } catch { $blocked = $true }
    if (-not $blocked) { throw 'English-only Whisper weights passed RU/EN preflight.' }
    $arguments.WhisperModel = Join-Path $weights 'ggml-base.bin'

    $badAsr = Join-Path $weights 'corrupt.bin'
    Mock-File $badAsr ([byte[]](65,66,67,68))
    $arguments.WhisperModel = $badAsr
    $blocked = $false
    try { & $prepare @arguments | Out-Null } catch { $blocked = $true }
    if (-not $blocked) { throw 'Corrupt Whisper weights passed preflight.' }
    $arguments.WhisperModel = Join-Path $weights 'ggml-base.bin'
    
    # CheckOnly must reject corruption after original staging, too.
    $stagedWhisper = Join-Path $app 'models/stt/ggml-base.bin'
    Mock-File $stagedWhisper ([byte[]](65,66,67,68))
    $blocked = $false
    try { & $prepare -AppDirectory $app -CheckOnly -RequireAsr | Out-Null } catch { $blocked = $true }
    if (-not $blocked) { throw 'CheckOnly accepted a damaged installed Whisper model.' }

    # Bad/mismatched Windows DLLs are not usable with the x64 native engines.
    # Do not ship a package which passes only the EXE header check.
    $badEngine = Join-Path $root 'wrong-runtime'
    $badApp = Join-Path $root 'wrong-runtime-app'
    New-Item -ItemType Directory -Force -Path $badEngine,$badApp | Out-Null
    Mock-Exe (Join-Path $badEngine 'llama-server.exe')
    Mock-Exe (Join-Path $badEngine 'wrong.dll') 0x14c
    Mock-File (Join-Path $badApp 'Ember.exe') ([byte[]](77,90))
    Mock-File (Join-Path $badApp 'Qt6Core.dll') ([byte[]](77,90))
    $badArgs = @{
        AppDirectory = $badApp
        LlamaServer = (Join-Path $badEngine 'llama-server.exe')
        ModelGguf = (Join-Path $weights 'local.gguf')
    }
    $blocked = $false
    try { & $prepare @badArgs | Out-Null } catch { $blocked = $true }
    if (-not $blocked) { throw 'x86 companion DLL was accepted for x64 engine.' }
    if (Test-Path (Join-Path $badApp 'engines/llama/llama-server.exe')) {
        throw 'Invalid runtime DLL did not fail before staging.'
    }

    # CheckOnly must inspect already staged companions rather than just the
    # required executable and weights.
    $installedDll = Join-Path $app 'engines/llama/ggml-base.dll'
    Mock-Exe $installedDll 0x14c
    $blocked = $false
    try { & $prepare -AppDirectory $app -CheckOnly | Out-Null } catch { $blocked = $true }
    if (-not $blocked) { throw 'CheckOnly accepted an incompatible staged DLL.' }

    # A nested directory junction must never redirect stage writes outside
    # AppDirectory, even though AppDirectory itself is a regular directory.
    $linkedApp = Join-Path $root 'junction-app'
    $outside = Join-Path $root 'outside-stage'
    New-Item -ItemType Directory -Force -Path $linkedApp,$outside | Out-Null
    Mock-File (Join-Path $linkedApp 'Ember.exe') ([byte[]](77,90))
    Mock-File (Join-Path $linkedApp 'Qt6Core.dll') ([byte[]](77,90))
    New-Item -ItemType Junction -Path (Join-Path $linkedApp 'engines') -Target $outside | Out-Null
    $linkedArgs = @{
        AppDirectory = $linkedApp
        LlamaServer = (Join-Path $eng 'llama-server.exe')
        ModelGguf = (Join-Path $weights 'local.gguf')
    }
    $blocked = $false
    try { & $prepare @linkedArgs | Out-Null } catch { $blocked = $true }
    if (-not $blocked) { throw 'A nested junction escaped the application root.' }
    if (@(Get-ChildItem -LiteralPath $outside -Force).Count -ne 0) {
        throw 'An offline asset was written through the nested junction.'
    }

    Write-Host 'Offline Windows text-first staging regressions passed.'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

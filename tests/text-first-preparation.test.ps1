Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Join-Path ([System.IO.Path]::GetTempPath()) ('ember-offline-text-' + [guid]::NewGuid().ToString('N'))
$prepare = Join-Path (Split-Path -Parent $PSScriptRoot) 'release/prepare-text-first.ps1'
function Mock-File([string]$Path, [byte[]]$Magic) {
    $raw = New-Object byte[] 256
    [array]::Copy($Magic, $raw, $Magic.Length)
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
    Mock-File (Join-Path $eng 'llama-server.exe') ([byte[]](77,90))
    Mock-File (Join-Path $eng 'ggml-base.dll') ([byte[]](77,90))
    Mock-File (Join-Path $weights 'local.gguf') ([byte[]](71,71,85,70))
    $arguments = @{
        AppDirectory = $app
        LlamaServer = (Join-Path $eng 'llama-server.exe')
        ModelGguf = (Join-Path $weights 'local.gguf')
    }
    & $prepare @arguments
    & $prepare -AppDirectory $app -CheckOnly
    & $prepare @arguments
    if (-not (Test-Path (Join-Path $app 'engines/llama/ggml-base.dll'))) {
        throw 'Required sibling engine DLL was not staged.'
    }
    $destination = Join-Path $app 'models/llm/model.gguf'
    $before = (Get-FileHash $destination -Algorithm SHA256).Hash
    $alternate = Join-Path $weights 'different.gguf'
    Mock-File $alternate ([byte[]](71,71,85,70))
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
    Write-Host 'Offline Windows text-first staging regressions passed.'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

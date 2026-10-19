[CmdletBinding()]
param(
    [ValidateSet('coordinator','responder')][string]$Role = 'responder',
    [ValidateSet('connectkit','tx1')][string]$Board = 'connectkit',
    [string]$NcsRoot = $env:NCS_ROOT,
    [string]$Python = 'python',
    [string]$SdkRoot = $env:ZEPHYR_SDK_INSTALL_DIR,
    [string]$BuildDirectory = '',
    [switch]$Pristine
)

$ErrorActionPreference = 'Stop'
if (-not $NcsRoot) {
    throw 'Set NCS_ROOT or pass -NcsRoot with the nRF Connect SDK path.'
}
if (-not (Test-Path -LiteralPath (Join-Path $NcsRoot 'zephyr'))) {
    throw "NCS_ROOT does not contain zephyr/: $NcsRoot"
}

$env:ZEPHYR_BASE = Join-Path $NcsRoot 'zephyr'
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'zephyr'
if ($SdkRoot) {
    $env:ZEPHYR_SDK_INSTALL_DIR = $SdkRoot
}

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$boardRoot = (Resolve-Path -LiteralPath (Join-Path $root '..\..')).Path
$conf = Join-Path $root "$Role.conf"
$overlay = Join-Path $root "boards\$Board.overlay"
$localCache = Join-Path $env:TEMP 'nrf54l15-sync-minimal-zephyr-cache'
$build = if ($BuildDirectory) {
    $BuildDirectory
} else {
    Join-Path $env:TEMP "nrf54l15-sync-minimal-$Role`_$Board"
}

$args = @(
    '-m','west','build','-d',$build,
    '-b','nrf54l15_connectkit/nrf54l15/cpuapp',
    $root
)
if ($Pristine) { $args += @('-p','always') }
$args += @('--',
    "-DBOARD_ROOT=$($boardRoot -replace '\\','/')",
    "-DZEPHYR_EXTRA_MODULES=$($boardRoot -replace '\\','/')",
    "-DEXTRA_CONF_FILE=$($conf -replace '\\','/')",
    "-DDTC_OVERLAY_FILE=$($overlay -replace '\\','/')",
    "-DUSER_CACHE_DIR=$($localCache -replace '\\','/')"
)

Push-Location -LiteralPath $root
try {
    & $Python @args
    if ($LASTEXITCODE -ne 0) { throw "sync_minimal build failed: $Role/$Board" }
} finally {
    Pop-Location
}

Write-Host "Build output: $build"

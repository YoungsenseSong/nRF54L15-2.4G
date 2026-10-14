[CmdletBinding()]
param(
    [ValidateSet('tx','rx')][string]$Target = 'tx',
    [string]$NcsRoot = 'D:\nRF54L15\NCS-Project',
    [string]$Python = 'E:\Anaconda\python.exe',
    [string]$SdkRoot = 'C:\Users\22209\zephyr-sdk-0.17.1',
    [string]$BuildDirectory = '',
    [switch]$Pristine
)
$ErrorActionPreference = 'Stop'
$env:ZEPHYR_BASE = Join-Path $NcsRoot 'zephyr'
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'zephyr'
$env:ZEPHYR_SDK_INSTALL_DIR = $SdkRoot
$build = if ($BuildDirectory) { $BuildDirectory } else { Join-Path $PSScriptRoot "build_$Target" }
$westArgs = @('-m','west','build','-d',$build,'-b','nrf54l15_connectkit/nrf54l15/cpuapp',
    (Join-Path $PSScriptRoot "applications\rf_link_$Target"))
if ($Pristine) { $westArgs += @('-p','always') }
$westArgs += @('--','-UBOARD_ROOT',"-DUSER_CACHE_DIR=$PSScriptRoot/zephyr_cache")
if ($Target -eq 'rx') {
    $westArgs += @('-DEXTRA_CONF_FILE=ch0_spis.conf;rx_scheduler.conf',
        '-DEXTRA_DTC_OVERLAY_FILE=ch0_spis.overlay')
}
$workspace = Join-Path $PSScriptRoot "ncs_workspace_$Target"
if (Test-Path -LiteralPath $workspace) { throw "Temporary SDK link already exists: $workspace" }
New-Item -ItemType Junction -Path $workspace -Target (Resolve-Path -LiteralPath $NcsRoot).Path | Out-Null
Push-Location -LiteralPath $workspace
try {
    & $Python @westArgs
    if ($LASTEXITCODE -ne 0) { throw "nRF $Target build failed." }
} finally {
    Pop-Location
    $link = Get-Item -LiteralPath $workspace -Force
    if ($link.LinkType -ne 'Junction') { throw 'Temporary SDK path is no longer a junction.' }
    Remove-Item -LiteralPath $workspace -Force
}
Write-Host "Build output: $build"

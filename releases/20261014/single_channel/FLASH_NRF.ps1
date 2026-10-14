[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('tx','rx')][string]$Target,
    [Parameter(Mandatory=$true)][string]$ProbeUid,
    [string]$Python = 'F:\OliverS\AI_Embedded\stm32-fpga-mcp\.venv\Scripts\python.exe'
)
$ErrorActionPreference = 'Stop'
$expected = if ($Target -eq 'tx') { '7FE202A5155825E005A9F1EBB0DEC145' } else { '820D9A5F0F3BA22B8E4ES' }
if ($ProbeUid -ne $expected) { throw "Unexpected $Target probe UID." }
$files = if ($Target -eq 'tx') {
    @('nrf54l15_tx_flpr.hex','nrf54l15_tx_cpuapp.hex')
} else { @('nrf54l15_rx_ch0_spis_repaired.hex') }
foreach ($file in $files) {
    $image = Join-Path $PSScriptRoot "images\$file"
    if (-not (Test-Path -LiteralPath $image)) { throw "Missing image: $image" }
    & $Python -m pyocd flash --target nrf54l --uid $ProbeUid --erase sector $image
    if ($LASTEXITCODE -ne 0) { throw "Programming failed: $file" }
}

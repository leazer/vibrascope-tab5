# @file build-tab5-windows.ps1
# @author 煲仔卤煮(ZealerLu)
# @date 2026-09-10
#
# @copyright Copyright (c) 2026
#
param(
    [string]$Port = '',
    [switch]$Flash,
    [switch]$Monitor,
    [int]$Baud = 921600,
    [string]$IdfPath = 'E:\Toolchain\esp-idf-v5.4.4',
    [string]$IdfToolsPath = 'E:\Toolchain\espressif-tools-v5.4.2'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $IdfToolsPath
. (Join-Path $IdfPath 'export.ps1')

$project = Join-Path (Split-Path -Parent $PSScriptRoot) 'platforms\tab5'
Push-Location $project
try {
    idf.py --no-ccache -B build-link -D IDF_TARGET=esp32p4 build
    if ($LASTEXITCODE -ne 0) { throw 'TAB5 build failed.' }
    if ($Flash) {
        if ([string]::IsNullOrWhiteSpace($Port)) { throw 'Use -Port COMx when -Flash is selected.' }
        idf.py --no-ccache -B build-link -p $Port -b $Baud flash
        if ($LASTEXITCODE -ne 0) { throw 'TAB5 flash failed.' }
    }
    if ($Monitor) {
        if ([string]::IsNullOrWhiteSpace($Port)) { throw 'Use -Port COMx when -Monitor is selected.' }
        idf.py -B build-link -p $Port monitor
    }
} finally {
    Pop-Location
}

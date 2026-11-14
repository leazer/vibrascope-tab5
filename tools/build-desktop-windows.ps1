param(
    [switch]$Run,
    [string]$SdlRoot = 'E:\Toolchain\SDL2-2.30.12-vc\SDL2-2.30.12',
    [string]$CMakePath = 'E:\Toolchain\cmake-ninja\cmake\data\bin\cmake.exe',
    [string]$NinjaPath = 'E:\Toolchain\cmake-ninja\bin\ninja.exe',
    [string]$VsDevCmd = 'D:\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build\windows-msvc-ninja'
$outputDir = Join-Path $repoRoot 'build\desktop'
$exePath = Join-Path $outputDir 'app_desktop_build.exe'
$sdlConfigDir = Join-Path $SdlRoot 'cmake'
$sdlIncludeDir = Join-Path $SdlRoot 'include'
$sdlCompatIncludeDir = Join-Path $buildDir 'sdl2-headers\SDL2'
$sdlDll = Join-Path $SdlRoot 'lib\x64\SDL2.dll'

foreach ($path in @($CMakePath, $NinjaPath, $VsDevCmd, $sdlConfigDir, $sdlIncludeDir, $sdlDll)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required build dependency is missing: $path"
    }
}

# LVGL's SDL driver includes <SDL2/SDL.h>, whereas the official VC package
# stores the headers directly under include/. Generate a compatibility copy
# in the build directory, without changing the shared toolchain SDK.
New-Item -ItemType Directory -Path $sdlCompatIncludeDir -Force | Out-Null
Get-ChildItem -LiteralPath $sdlIncludeDir -File -Filter '*.h' |
    Copy-Item -Destination $sdlCompatIncludeDir -Force

# Import Visual Studio's x64 compiler environment into this PowerShell process.
# VsDevCmd expands PATH inside cmd.exe (8191-character command limit). An IDF
# terminal or repeated toolchain activation can leave too little room for it.
# Initialize VS in a clean child environment; keep the caller's tools available
# to CMake afterward. Do not change machine/user environment settings.
$cmdExe = Join-Path $env:SystemRoot 'System32\cmd.exe'
$callerPath = $env:Path
$vsStart = [System.Diagnostics.ProcessStartInfo]::new()
$vsStart.FileName = $cmdExe
$vsStart.Arguments = "/d /c call `"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && set"
$vsStart.UseShellExecute = $false
$vsStart.CreateNoWindow = $true
$vsStart.RedirectStandardOutput = $true
$vsStart.RedirectStandardError = $true
$vsStart.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem"
foreach ($name in @($vsStart.Environment.Keys)) {
    if ($name -match '^(INCLUDE|LIB|LIBPATH|EXTERNAL_INCLUDE|__VSCMD.*|VSCMD_.*|VS\d+COMNTOOLS|VCINSTALLDIR|VCTools.*|DevEnvDir|VisualStudioVersion|WindowsSdk.*|UniversalCRT.*|UCRT.*|Framework.*|NETFX.*|ExtensionSdk.*)$') {
        [void]$vsStart.Environment.Remove($name)
    }
}
$vsProcess = [System.Diagnostics.Process]::new()
$vsProcess.StartInfo = $vsStart
try {
    [void]$vsProcess.Start()
    $vsOutput = $vsProcess.StandardOutput.ReadToEndAsync()
    $vsErrors = $vsProcess.StandardError.ReadToEndAsync()
    $vsProcess.WaitForExit()
    $vsEnvText = $vsOutput.GetAwaiter().GetResult()
    $vsErrorText = $vsErrors.GetAwaiter().GetResult()
    if ($vsProcess.ExitCode -ne 0) {
        throw "Failed to initialize the Visual Studio C++ build environment (exit $($vsProcess.ExitCode)). $vsErrorText"
    }
} finally {
    $vsProcess.Dispose()
}
foreach ($line in ($vsEnvText -split '\r?\n')) {
    $separator = $line.IndexOf('=')
    if ($separator -gt 0) {
        Set-Item -Path "Env:$($line.Substring(0, $separator))" -Value $line.Substring($separator + 1)
    }
}
# Deduplicate paths so repeated invocations do not accumulate compiler paths.
$pathEntries = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$env:Path = (($env:Path + ';' + $callerPath) -split ';' | Where-Object {
    $_ -and $pathEntries.Add($_.TrimEnd('\'))
}) -join ';'

& $CMakePath -S $repoRoot -B $buildDir -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$NinjaPath" `
    '-DCMAKE_BUILD_TYPE=Release' `
    '-DCMAKE_CXX_FLAGS=/D_USE_MATH_DEFINES /utf-8 /EHsc' `
    "-DSDL2_DIR=$sdlConfigDir" "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$outputDir"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

& $CMakePath --build $buildDir --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Desktop build failed.' }

New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$outputSdlDll = Join-Path $outputDir 'SDL2.dll'
if (!(Test-Path -LiteralPath $outputSdlDll) -or
    (Get-FileHash -LiteralPath $sdlDll).Hash -ne (Get-FileHash -LiteralPath $outputSdlDll).Hash) {
    Copy-Item -LiteralPath $sdlDll -Destination $outputSdlDll -Force
}

Write-Host "Built: $exePath"
if ($Run) {
    Start-Process -FilePath $exePath -WorkingDirectory $outputDir
}

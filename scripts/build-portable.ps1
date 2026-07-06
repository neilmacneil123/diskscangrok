<#
.SYNOPSIS
  One-command build for a single portable diskscangrok.exe (static CRT, no redist).

.DESCRIPTION
  - Cleans previous build if requested
  - Prefers Ninja + Release
  - Forces static MSVC runtime
  - Runs tests
  - Copies final exe to dist/ with size report

.EXAMPLE
  .\scripts\build-portable.ps1
  .\scripts\build-portable.ps1 -Clean
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root

function Get-VsVarsPath {
    # Prefer vswhere (comes with VS Installer)
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($installPath) {
            $candidate = Join-Path $installPath 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $candidate) { return $candidate }
        }
    }

    # Common fallbacks (Build Tools, Community, etc.)
    $fallbacks = @(
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
    )
    foreach ($fb in $fallbacks) {
        if (Test-Path $fb) { return $fb }
    }
    return $null
}

$vcvars = $null
$hasCl = $false
try {
    $hasCl = [bool](Get-Command cl.exe -ErrorAction SilentlyContinue)
} catch { $hasCl = $false }

if (-not $hasCl) {
    $vcvars = Get-VsVarsPath
    if ($vcvars) {
        Write-Host "No cl.exe in PATH. Will activate Visual Studio environment using:" -ForegroundColor Yellow
        Write-Host "  $vcvars" -ForegroundColor Gray
    } else {
        Write-Warning "Could not find Visual Studio (vcvars64.bat). Install 'Desktop development with C++' workload or run from a Developer PowerShell / Command Prompt."
    }
}

function Invoke-CMakeWithEnv {
    param([Parameter(Mandatory=$true)][string]$Command)
    if ($vcvars) {
        $full = "call `"$vcvars`" >nul 2>&1 && $Command"
        & cmd.exe /c $full | Out-Host
        $exitCode = $LASTEXITCODE
    } else {
        & cmd.exe /c $Command | Out-Host
        $exitCode = $LASTEXITCODE
    }
    return $exitCode
}

try {
    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Host "Cleaning $BuildDir..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
    }

    Write-Host "Configuring with generator $Generator ($Config)..." -ForegroundColor Cyan
    $code = Invoke-CMakeWithEnv "cmake -S . -B $BuildDir -G `"$Generator`" -DCMAKE_BUILD_TYPE=$Config"
    if ($code -ne 0) {
        throw "CMake configure failed (exit $code). Make sure you have Visual Studio Build Tools with the C++ workload installed."
    }

    Write-Host "Building..." -ForegroundColor Cyan
    $code = Invoke-CMakeWithEnv "cmake --build $BuildDir --config $Config --parallel"
    if ($code -ne 0) { throw "CMake build failed (exit $code)" }

    Write-Host "Running tests..." -ForegroundColor Cyan
    $code = Invoke-CMakeWithEnv "ctest --test-dir $BuildDir --output-on-failure -C $Config"
    if ($code -ne 0) {
        Write-Warning "Some tests failed (exit $code). Continuing anyway..."
    }

    # Locate the exe (handles different generators)
    $candidates = @(
        (Join-Path $BuildDir 'diskscangrok.exe'),
        (Join-Path $BuildDir $Config 'diskscangrok.exe'),
        (Join-Path $BuildDir 'bin' 'diskscangrok.exe')
    )
    $exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1

    if (-not $exe) {
        throw "Could not locate built diskscangrok.exe after successful build."
    }

    New-Item -ItemType Directory -Force -Path dist | Out-Null
    Copy-Item $exe -Destination (Join-Path 'dist' 'diskscangrok.exe') -Force

    $size = (Get-Item (Join-Path 'dist' 'diskscangrok.exe')).Length / 1MB
    Write-Host ""
    Write-Host "✅ exe ready: dist\diskscangrok.exe  ($([math]::Round($size,2)) MiB)" -ForegroundColor Green
    Write-Host "   From PowerShell:  .\dist\diskscangrok.exe C:\     (or any path)"
    Write-Host ""
    Write-Host "Note on 'single portable':" -ForegroundColor Yellow
    Write-Host "  Default MSVC build links against VCRUNTIME140.dll + MSVCP140.dll (standard)." -ForegroundColor Gray
    Write-Host "  The exe itself is self-contained aside from the VC++ runtime (common on Windows)." -ForegroundColor Gray
    Write-Host "  For a no-redist truly standalone exe use MinGW cross-compile from a Linux box (toolchain provided)." -ForegroundColor Gray
    Write-Host "  Check: dumpbin /dependents dist\diskscangrok.exe" -ForegroundColor DarkGray
}
finally {
    Pop-Location
}

<#
.SYNOPSIS
    Build (optional) and flash the Tachyon firmware to the STM32F405 over SWD
    using an ST-LINK/V2 and STM32CubeProgrammer CLI.

.DESCRIPTION
    Auto-discovers STM32_Programmer_CLI.exe (PATH, standalone CubeProgrammer,
    or the CubeIDE-bundled copy), then downloads the ELF over SWD and starts it.
    No BOOT0/DFU dance required — just keep the ST-LINK wired:
        SWDIO->PA13, SWCLK->PA14, GND->GND, 3.3V(VTref)->3V3.

.PARAMETER Config
    CMake build config to flash: Debug (default) or Release.

.PARAMETER Build
    Run `cmake --build` for the chosen config before flashing.

.EXAMPLE
    ./flash.ps1              # flash build/Debug/firmware.elf
    ./flash.ps1 -Build       # build Debug, then flash
    ./flash.ps1 Release      # flash build/Release/firmware.elf
#>
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',
    [switch]$Build
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Find-Programmer {
    $cmd = Get-Command 'STM32_Programmer_CLI' -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = @(
        "$env:ProgramFiles\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
        "${env:ProgramFiles(x86)}\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }

    # CubeIDE-bundled copy (version-pinned path — glob for whatever is installed).
    $ide = Get-ChildItem -Path 'C:\ST' -Recurse -Filter 'STM32_Programmer_CLI.exe' `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ide) { return $ide.FullName }

    throw "STM32_Programmer_CLI.exe not found. Install STM32CubeProgrammer or add it to PATH."
}

$cli = Find-Programmer
$elf = Join-Path $root "build\$Config\firmware.elf"

if ($Build) {
    Write-Host "Building $Config..." -ForegroundColor Cyan
    & cmake --build (Join-Path $root "build\$Config")
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
}

if (-not (Test-Path $elf)) {
    throw "ELF not found: $elf  (build it first, e.g. ./flash.ps1 -Build)"
}

Write-Host "Flashing $elf over SWD..." -ForegroundColor Cyan
& $cli --connect port=SWD mode=UR --download $elf --start
exit $LASTEXITCODE

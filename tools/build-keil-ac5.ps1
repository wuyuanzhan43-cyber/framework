# Compile + link the STM32F407VE firmware with the Keil ARM Compiler 5 toolchain,
# using exactly the flags the generated uVision project uses.
#
# Purpose: the analysis in projects/stm32f407/KEIL.md claims the Keil project
# builds. This script is the reproducible evidence for that claim, and a fallback
# path when you want a binary without opening the uVision GUI.
#
# Why not drive UV4 in batch mode? UV4 -b blocks on a modal dialog when launched
# from a non-interactive session (see KEIL.md "Known limitations"). This script
# calls armcc/armasm/armlink directly, so it works headlessly.
#
# Usage:  . .\tools\build-keil-ac5.ps1
#         . .\tools\build-keil-ac5.ps1 -OutputRoot build\keil-release -Optimise

[CmdletBinding()]
param(
    [string] $OutputRoot = 'build\keil',
    [switch] $Optimise,
    [string] $ArmDir = 'D:\keil5\stm32\ARM\ARMCC\bin'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$cc   = Join-Path $ArmDir 'armcc.exe'
$asm  = Join-Path $ArmDir 'armasm.exe'
$link = Join-Path $ArmDir 'armlink.exe'
$elf2 = Join-Path $ArmDir 'fromelf.exe'
foreach ($tool in @($cc, $asm, $link, $elf2)) {
    if (-not (Test-Path $tool)) { throw "ARM Compiler 5 tool not found: $tool (pass -ArmDir)" }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

# PowerShell 5.1 turns a native command's stderr into a terminating error when
# $ErrorActionPreference is 'Stop', which would abort the build on the first
# compiler warning. Run native tools with the preference relaxed and judge the
# result by the exit code only.
function Invoke-Native {
    param([string] $Exe, [string[]] $Arguments)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = & $Exe @Arguments 2>&1 | ForEach-Object { $_.ToString() }
        return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = @($lines) }
    } finally { $ErrorActionPreference = $previous }
}

try {
    New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
    $outAbs = (Resolve-Path $OutputRoot).Path
    Get-ChildItem $outAbs -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in '.o', '.axf', '.hex', '.bin', '.map', '.txt' } |
        Remove-Item -Force

    $includes = @(
        'framework\include'
        'components\protocol\include'
        'app\include'
        'ports\stm32f4\include'
        'ports\freertos\include'
        'boards\stm32f407ve'
        'projects\stm32f407\config'
        'third_party\hal-src\Inc'
        'third_party\device-src\Include'
        'third_party\cmsis-src\CMSIS\Core\Include'
        'third_party\freertos-src\include'
        'third_party\freertos-src\portable\RVDS\ARM_CM4F'
    ) | ForEach-Object { "-I$_" }

    $optim = if ($Optimise) { '-O2' } else { '-O0' }
    # --c99 is required: the HAL and this framework use C99 declarations.
    # --gnu is NOT needed and is left off, matching the uVision project (uGnu=0).
    $cFlags = @('-c', '--cpu', 'Cortex-M4.fp', '--c99', '-g', $optim, '--apcs=interwork',
                '-DSTM32F407xx', '-DUSE_HAL_DRIVER') + $includes

    $cSources = @(
        'framework\core\io.c'
        'framework\core\ring.c'
        'components\protocol\protocol.c'
        'app\demo_app.c'
        'boards\stm32f407ve\board.c'
        'ports\stm32f4\gpio.c'
        'ports\stm32f4\uart.c'
        'ports\freertos\runtime.c'
        'third_party\freertos-src\tasks.c'
        'third_party\freertos-src\queue.c'
        'third_party\freertos-src\list.c'
        'third_party\freertos-src\portable\RVDS\ARM_CM4F\port.c'
        'third_party\hal-src\Src\stm32f4xx_hal.c'
        'third_party\hal-src\Src\stm32f4xx_hal_cortex.c'
        'third_party\hal-src\Src\stm32f4xx_hal_rcc.c'
        'third_party\hal-src\Src\stm32f4xx_hal_rcc_ex.c'
        'third_party\hal-src\Src\stm32f4xx_hal_gpio.c'
        'third_party\hal-src\Src\stm32f4xx_hal_uart.c'
        'third_party\hal-src\Src\stm32f4xx_hal_dma.c'
        'third_party\hal-src\Src\stm32f4xx_hal_dma_ex.c'
        'third_party\hal-src\Src\stm32f4xx_hal_flash.c'
        'third_party\hal-src\Src\stm32f4xx_hal_flash_ex.c'
        'third_party\hal-src\Src\stm32f4xx_hal_pwr.c'
        'third_party\hal-src\Src\stm32f4xx_hal_pwr_ex.c'
        'third_party\device-src\Source\Templates\system_stm32f4xx.c'
        'projects\stm32f407\main.c'
        'projects\stm32f407\interrupts.c'
    )

    $objects = @()
    $compileFailures = 0
    $warningCount = 0
    Write-Host "Compiling $($cSources.Count) C sources with ARM Compiler 5 ($optim) ..." -ForegroundColor Cyan
    foreach ($src in $cSources) {
        $obj = Join-Path $outAbs (([System.IO.Path]::GetFileNameWithoutExtension($src)) + '.o')
        $run = Invoke-Native $cc ($cFlags + @("-o$obj", $src))
        $warningCount += @($run.Output | Where-Object { $_ -match 'Warning:' }).Count
        if ($run.ExitCode -ne 0) {
            $compileFailures++
            Write-Host "  FAIL $src" -ForegroundColor Red
            ($run.Output | Where-Object { $_ -match 'rror' } | Select-Object -First 5) |
                ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
        }
        $objects += $obj
    }
    if ($compileFailures -gt 0) { throw "$compileFailures source file(s) failed to compile" }
    if ($warningCount -gt 0) {
        # Not fatal: AC5 reports dead code that a fully unconfigured board_config.h
        # creates legitimately (see KEIL.md). Surface the count so it is not missed.
        Write-Host "  $warningCount compiler warning(s) - run with the GUI or add -Wall to inspect" -ForegroundColor Yellow
    }

    $startupObj = Join-Path $outAbs 'startup_stm32f407xx.o'
    $asmRun = Invoke-Native $asm @('--cpu', 'Cortex-M4.fp', '--apcs=interwork', '-g',
                                   "-o$startupObj",
                                   'third_party\device-src\Source\Templates\arm\startup_stm32f407xx.s')
    if ($asmRun.ExitCode -ne 0) {
        $asmRun.Output | ForEach-Object { Write-Host $_ -ForegroundColor Red }
        throw 'startup_stm32f407xx.s failed to assemble'
    }
    $objects += $startupObj

    $axf = Join-Path $outAbs 'fw_f407.axf'
    Write-Host 'Linking ...' -ForegroundColor Cyan
    # --list writes the map file; --map takes no argument in ARM Compiler 5.
    # Output goes to a relative filename: armcc/armlink mangle non-ASCII absolute
    # paths, so the link runs with the build directory as the working directory.
    Push-Location $outAbs
    try {
        $linkArgs = @('--cpu', 'Cortex-M4.fp',
                      '--scatter', (Join-Path $repoRoot 'projects\stm32f407\stm32f407ve.sct'),
                      '--list', 'fw_f407.map',
                      '--info', 'sizes', '--info', 'totals',
                      '--entry', 'Reset_Handler', '--strict',
                      '--libpath', (Join-Path (Split-Path -Parent $ArmDir) 'lib'),
                      '-o', 'fw_f407.axf') + ($objects | ForEach-Object { Split-Path -Leaf $_ })
        $linkRun = Invoke-Native $link $linkArgs
        $linkRun.Output | ForEach-Object { Write-Host $_ }
        if ($linkRun.ExitCode -ne 0) { throw "armlink failed with exit code $($linkRun.ExitCode)" }

        $null = Invoke-Native $elf2 @('--i32combined', '--output', 'fw_f407.hex', 'fw_f407.axf')
        $null = Invoke-Native $elf2 @('--bin', '--output', 'fw_f407.bin', 'fw_f407.axf')
    } finally { Pop-Location }

    Write-Host ''
    Write-Host 'Build artefacts:' -ForegroundColor Green
    Get-ChildItem $outAbs -File |
        Where-Object { $_.Name -match 'fw_f407\.(axf|hex|bin|map)' } |
        ForEach-Object { '  {0,-16} {1,9:N0} bytes' -f $_.Name, $_.Length }
    Write-Host ''
    Write-Host 'Memory summary (Total ROM Size is what gets flashed):' -ForegroundColor Green
    Get-Content (Join-Path $outAbs 'fw_f407.map') |
        Select-String -Pattern 'Total RO  Size|Total RW  Size|Total ROM Size' |
        ForEach-Object { '  ' + $_.Line.Trim() }
} finally { Pop-Location }

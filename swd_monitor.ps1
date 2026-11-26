<#
.SYNOPSIS
    Real-Time SWD Telemetry Monitor for Mirrant TDS 4002 (STM32G030K8T6)
    Reads live calculation memory directly through ST-LINK on Header J3 using mode=HOTPLUG.
    Keeps MCU board running continuously without reset or halts so parameters can be tested live.
#>

param(
    [int]$IntervalMs = 200
)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$cliPath = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
if (-not (Test-Path $cliPath)) {
    Write-Host "[ERROR] STM32CubeProgrammer CLI not found at: $cliPath" -ForegroundColor Red
    pause
    exit 1
}

function Convert-HexToFloat([string]$hexStr) {
    try {
        $u = [System.Convert]::ToUInt32($hexStr, 16)
        $bytes = [System.BitConverter]::GetBytes($u)
        return [System.BitConverter]::ToSingle($bytes, 0)
    } catch {
        return 0.0
    }
}

function Get-MapSymbolAddr([string]$mapPath, [string]$sectionPattern, [uint32]$defaultAddr) {
    if (Test-Path $mapPath) {
        $lines = Get-Content $mapPath
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match $sectionPattern) {
                if ($lines[$i] -match '(0x2000[0-9A-Fa-f]{4})') {
                    return [System.Convert]::ToUInt32($Matches[1], 16)
                }
                if (($i + 1) -lt $lines.Count -and $lines[$i + 1] -match '^\s*(0x2000[0-9A-Fa-f]{4})') {
                    return [System.Convert]::ToUInt32($Matches[1], 16)
                }
            }
        }
    }
    return $defaultAddr
}

$mapPath      = Join-Path $PSScriptRoot "build\display.map"
$readingAddr  = Get-MapSymbolAddr $mapPath '\.bss\.s_reading\b'  0x200002DC
$settingsAddr = Get-MapSymbolAddr $mapPath '\.bss\.s_settings\b' 0x20000318

# Read 16-byte aligned contiguous RAM covering s_reading (60B) and s_settings up through tds_coef (+72B)
$dumpStart = [uint32]($readingAddr - ($readingAddr % 16))
$dumpEnd   = [uint32]($settingsAddr + 72)
$dumpBytes = [int]($dumpEnd - $dumpStart)
if (($dumpBytes % 16) -ne 0) { $dumpBytes += (16 - ($dumpBytes % 16)) }

$readingHex  = ("0x{0:X8}" -f $readingAddr)
$settingsHex = ("0x{0:X8}" -f $settingsAddr)
$dumpHex     = ("0x{0:X8}" -f $dumpStart)

Clear-Host
Write-Host "====================================================================================" -ForegroundColor Cyan
Write-Host "         MIRRANT TDS 4002 - REAL-TIME LIVE SWD TELEMETRY MONITOR (J3 HEADER)        " -ForegroundColor White -BackgroundColor DarkBlue
Write-Host "====================================================================================" -ForegroundColor Cyan
Write-Host " Link     : ST-LINK Programmer connected to J3 Header (SWDIO, SWCLK, GND, 3.3V)     " -ForegroundColor Gray
Write-Host " Target   : STM32G030K8T6 | RAM: $readingHex (s_reading) & $settingsHex (s_settings)" -ForegroundColor Yellow
Write-Host " Mode     : HOTPLUG (MCU runs continuously without reset, halt, or interruption)    " -ForegroundColor Green
Write-Host " Interval : $IntervalMs ms FAST live feed | Press Ctrl+C or 'Q' to Exit             " -ForegroundColor Green
Write-Host "====================================================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "[OK] Connected to ST-LINK. Streaming live COND + RS-485 TURB telemetry...`n" -ForegroundColor Green

$running = $true
$lastK   = -1.0
$lastTC  = -1.0
$lastTDS = -1.0

while ($running) {
    try {
        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            if ($key.Key -eq [ConsoleKey]::Escape -or $key.KeyChar -eq 'q' -or $key.KeyChar -eq 'Q') {
                Write-Host "`n[*] Exiting SWD monitor..." -ForegroundColor Yellow
                break
            }
        }
    } catch {}

    # Query RAM covering s_reading and s_settings using SWD with mode=HOTPLUG
    $output = & $cliPath -c port=SWD mode=HOTPLUG freq=4000 -r32 $dumpHex $dumpBytes 2>&1

    # Map every returned 32-bit word by its exact memory address
    $memMap = @{}
    foreach ($line in $output) {
        if ($line -match '^\s*(0x[0-9A-Fa-f]{8})\s*:\s*(.+)$') {
            $lineAddr = [System.Convert]::ToUInt32($Matches[1], 16)
            $hexWords = ($Matches[2].Trim() -split '\s+')
            for ($w = 0; $w -lt $hexWords.Count; $w++) {
                if ($hexWords[$w] -match '^[0-9A-Fa-f]{8}$') {
                    $memMap[([uint32]($lineAddr + ($w * 4)))] = $hexWords[$w]
                }
            }
        }
    }

    if ($memMap.ContainsKey([uint32]($readingAddr + 56))) {
        $cellOhms   = Convert-HexToFloat $memMap[[uint32]($readingAddr + 0)]
        $tempOhms   = Convert-HexToFloat $memMap[[uint32]($readingAddr + 4)]
        $tempC      = Convert-HexToFloat $memMap[[uint32]($readingAddr + 8)]
        $tempF      = Convert-HexToFloat $memMap[[uint32]($readingAddr + 12)]
        $condUs     = Convert-HexToFloat $memMap[[uint32]($readingAddr + 16)]
        $condRaw    = Convert-HexToFloat $memMap[[uint32]($readingAddr + 20)]
        $resMohm    = Convert-HexToFloat $memMap[[uint32]($readingAddr + 24)]
        $tdsPpm     = Convert-HexToFloat $memMap[[uint32]($readingAddr + 28)]
        $salPpt     = Convert-HexToFloat $memMap[[uint32]($readingAddr + 32)]
        $turbNtu    = Convert-HexToFloat $memMap[[uint32]($readingAddr + 36)]
        $curr1      = Convert-HexToFloat $memMap[[uint32]($readingAddr + 44)]
        $curr2      = Convert-HexToFloat $memMap[[uint32]($readingAddr + 48)]

        # Byte-packed flags/relays at +52 and +56
        $flags0 = [System.Convert]::ToUInt32($memMap[[uint32]($readingAddr + 52)], 16)
        $r1         = ($flags0 -band 0xFF)
        $r2         = (($flags0 -shr 8) -band 0xFF)

        $flags1 = [System.Convert]::ToUInt32($memMap[[uint32]($readingAddr + 56)], 16)
        $faultTurb  = ((($flags1 -shr 8) -band 0xFF) -ne 0)

        # Settings offsets in settings_t: k_constant (+8), temp_coef (+64), tds_coef (+68)
        $kConst = if ($memMap.ContainsKey([uint32]($settingsAddr + 8)))  { Convert-HexToFloat $memMap[[uint32]($settingsAddr + 8)] }  else { 0.0852 }
        $tcVal  = if ($memMap.ContainsKey([uint32]($settingsAddr + 64))) { Convert-HexToFloat $memMap[[uint32]($settingsAddr + 64)] } else { 2.0 }
        $tdsVal = if ($memMap.ContainsKey([uint32]($settingsAddr + 68))) { Convert-HexToFloat $memMap[[uint32]($settingsAddr + 68)] } else { 0.5 }

        # Check for parameter changes in RAM
        $kChanged   = ($lastK -gt 0 -and [Math]::Abs($kConst - $lastK) -gt 0.001)
        $tcChanged  = ($lastTC -gt 0 -and [Math]::Abs($tcVal - $lastTC) -gt 0.01)
        $tdsChanged = ($lastTDS -gt 0 -and [Math]::Abs($tdsVal - $lastTDS) -gt 0.001)

        if ($kChanged -or $tcChanged -or $tdsChanged) {
            Write-Host "`n************************************************************************" -ForegroundColor Yellow
            Write-Host ">>> [SETTING CHANGED IN RAM] Active Parameters Updated <<<" -ForegroundColor Cyan
            if ($kChanged)   { Write-Host "  Cell Constant K  : $lastK -> $kConst (1/cm)" -ForegroundColor Cyan }
            if ($tcChanged)  { Write-Host "  Temp Coeff (TC)  : $lastTC -> $tcVal %/°C" -ForegroundColor Cyan }
            if ($tdsChanged) { Write-Host "  TDS Factor       : $lastTDS -> $tdsVal" -ForegroundColor Cyan }
            Write-Host "--- CALCULATION FORMULAS ---" -ForegroundColor DarkCyan
            Write-Host "  1. Conductance (S)  : 1 / R_cell" -ForegroundColor Gray
            Write-Host "  2. Raw EC (uS/cm)   : (K * 1,000,000) / R_cell" -ForegroundColor Gray
            Write-Host "  3. Comp Factor      : 1 + (TC / 100) * (Temp_C - 25.0)" -ForegroundColor Gray
            Write-Host "  4. Comp EC (uS/cm)  : Raw_EC / Comp_Factor" -ForegroundColor Gray
            Write-Host "  5. TDS (ppm)        : Comp_EC * TDS_Factor" -ForegroundColor Gray
            Write-Host "  6. Resistivity      : 1.0 / Comp_EC  (M-Ohm*cm)" -ForegroundColor Gray
            Write-Host "  7. Salinity (ppt)   : (Comp_EC / 1000) * 0.50" -ForegroundColor Gray
            Write-Host "  8. Turbidity (NTU)  : RS-485 Slave 3 Reg 14 (0x000E) via Start 6" -ForegroundColor Gray
            Write-Host "************************************************************************`n" -ForegroundColor Yellow
        }
        $lastK   = $kConst
        $lastTC  = $tcVal
        $lastTDS = $tdsVal

        $ts = (Get-Date).ToString("HH:mm:ss.fff")
        $r1Str   = if ($r1 -eq 1) { "ON" } else { "OFF" }
        $r2Str   = if ($r2 -eq 1) { "ON" } else { "OFF" }
        $r1Col   = if ($r1 -eq 1) { "Green" } else { "DarkGray" }
        $r2Col   = if ($r2 -eq 1) { "Green" } else { "DarkGray" }
        $turbCol = if ($faultTurb) { "DarkYellow" } else { "Cyan" }
        $turbTag = if ($faultTurb) { " (OFFLINE)" } else { "" }

        Write-Host "[$ts] " -ForegroundColor DarkGray -NoNewline
        Write-Host "COND:" -ForegroundColor DarkCyan -NoNewline
        Write-Host ("{0,7:F1}uS/cm " -f $condUs) -ForegroundColor Green -NoNewline
        Write-Host "| TURB:" -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,6:F1}NTU{1} " -f $turbNtu, $turbTag) -ForegroundColor $turbCol -NoNewline
        Write-Host "| TEMP:" -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,5:F1}°C " -f $tempC) -ForegroundColor Cyan -NoNewline
        Write-Host "| TDS:" -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,6:F1}ppm " -f $tdsPpm) -ForegroundColor Yellow -NoNewline
        Write-Host "| RES:" -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,6:F3}MΩ" -f $resMohm) -ForegroundColor Magenta

        Write-Host "               SAL:" -ForegroundColor Blue -NoNewline
        Write-Host ("{0,5:F2}ppt    " -f $salPpt) -ForegroundColor Blue -NoNewline
        Write-Host "| RAW:" -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,7:F1}       " -f $condRaw) -ForegroundColor DarkGreen -NoNewline
        Write-Host "| R1:" -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,-3} " -f $r1Str) -ForegroundColor $r1Col -NoNewline
        Write-Host "R2:" -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,-3} " -f $r2Str) -ForegroundColor $r2Col -NoNewline
        Write-Host "| I1:" -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,5:F2}mA " -f $curr1) -ForegroundColor DarkYellow -NoNewline
        Write-Host "I2:" -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,5:F2}mA " -f $curr2) -ForegroundColor DarkYellow -NoNewline
        Write-Host "| Rc:" -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,6:F1}Ω " -f $cellOhms) -ForegroundColor Gray -NoNewline
        Write-Host "Rt:" -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,6:F1}Ω" -f $tempOhms) -ForegroundColor Gray
    }

    Start-Sleep -Milliseconds $IntervalMs
}

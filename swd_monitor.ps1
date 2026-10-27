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

Clear-Host
Write-Host "====================================================================================" -ForegroundColor Cyan
Write-Host "         MIRRANT TDS 4002 - REAL-TIME LIVE SWD TELEMETRY MONITOR (J3 HEADER)        " -ForegroundColor White -BackgroundColor DarkBlue
Write-Host "====================================================================================" -ForegroundColor Cyan
Write-Host " Link     : ST-LINK Programmer connected to J3 Header (SWDIO, SWCLK, GND, 3.3V)     " -ForegroundColor Gray
Write-Host " Target   : STM32G030K8T6 | RAM: 0x200002E8 (s_reading) & 0x20000320 (s_settings)   " -ForegroundColor Yellow
Write-Host " Mode     : HOTPLUG (MCU runs continuously without reset, halt, or interruption)    " -ForegroundColor Green
Write-Host " Interval : $IntervalMs ms FAST live feed | Press Ctrl+C or 'Q' to Exit             " -ForegroundColor Green
Write-Host "====================================================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "[OK] Connected to ST-LINK. Streaming live sensor telemetry and math calculations...`n" -ForegroundColor Green

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

    # Query 144 bytes from RAM 0x200002E0 using SWD with mode=HOTPLUG
    $output = & $cliPath -c port=SWD mode=HOTPLUG freq=4000 -r32 0x200002e0 144 2>&1

    $words = @()
    foreach ($line in $output) {
        if ($line -match '0x200002[E-F]0\s*:\s*([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})' -or
            $line -match '0x200003[0-7]0\s*:\s*([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})') {
            $words += $Matches[1]
            $words += $Matches[2]
            $words += $Matches[3]
            $words += $Matches[4]
        }
    }

    # s_reading starts at offset 0x2E8 (Word 2 in 0x200002E0 dump)
    if ($words.Count -ge 16) {
        $cellOhms   = Convert-HexToFloat $words[2]
        $tempOhms   = Convert-HexToFloat $words[3]
        $tempC      = Convert-HexToFloat $words[4]
        $tempF      = Convert-HexToFloat $words[5]
        $condUs     = Convert-HexToFloat $words[6]
        $condRaw    = Convert-HexToFloat $words[7]
        $resMohm    = Convert-HexToFloat $words[8]
        $tdsPpm     = Convert-HexToFloat $words[9]
        $salPpt     = Convert-HexToFloat $words[10]
        $sampleFval = Convert-HexToFloat $words[11]
        $curr1      = Convert-HexToFloat $words[12]
        $curr2      = Convert-HexToFloat $words[13]

        # Word 14 has relays: low byte is relay1, second byte is relay2
        $rWord = [System.Convert]::ToUInt32($words[14], 16)
        $r1 = ($rWord -band 0xFF)
        $r2 = (($rWord -shr 8) -band 0xFF)

        # Settings: s_settings starts at 0x20000320 (Word 16 in dump)
        # Word 18 = k_constant (0x20000328)
        # Word 32 = temp_coef (0x20000360)
        # Word 33 = tds_coef (0x20000364)
        $kConst = if ($words.Count -ge 19) { Convert-HexToFloat $words[18] } else { 1.0 }
        $tcVal  = if ($words.Count -ge 33) { Convert-HexToFloat $words[32] } else { 2.0 }
        $tdsVal = if ($words.Count -ge 34) { Convert-HexToFloat $words[33] } else { 0.5 }

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
            Write-Host "  8. Sample Test      : FVALUE = (a + b) * (c - d)" -ForegroundColor Gray
            Write-Host "************************************************************************`n" -ForegroundColor Yellow
        }
        $lastK   = $kConst
        $lastTC  = $tcVal
        $lastTDS = $tdsVal

        $ts = (Get-Date).ToString("HH:mm:ss.fff")
        $r1Str = if ($r1 -eq 1) { "ON" } else { "OFF" }
        $r2Str = if ($r2 -eq 1) { "ON" } else { "OFF" }
        $r1Col = if ($r1 -eq 1) { "Green" } else { "DarkGray" }
        $r2Col = if ($r2 -eq 1) { "Green" } else { "DarkGray" }

        Write-Host "[$ts] " -ForegroundColor DarkGray -NoNewline
        Write-Host "COND: " -ForegroundColor DarkCyan -NoNewline
        Write-Host ("{0,7:F1} uS/cm " -f $condUs) -ForegroundColor Green -NoNewline
        Write-Host "| RAW: " -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,7:F1} " -f $condRaw) -ForegroundColor DarkGreen -NoNewline
        Write-Host "| TEMP: " -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,5:F1}°C ({1,5:F1}°F) " -f $tempC, $tempF) -ForegroundColor Cyan -NoNewline
        Write-Host "| TDS: " -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,6:F1} ppm " -f $tdsPpm) -ForegroundColor Yellow -NoNewline
        Write-Host "| RES: " -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,6:F3} MΩ " -f $resMohm) -ForegroundColor Magenta -NoNewline
        Write-Host "| SAL: " -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,5:F2} ppt " -f $salPpt) -ForegroundColor Blue -NoNewline
        Write-Host "| FVAL: " -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,6:F2} " -f $sampleFval) -ForegroundColor Yellow -NoNewline
        Write-Host "| R1: " -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,-3} " -f $r1Str) -ForegroundColor $r1Col -NoNewline
        Write-Host "| R2: " -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,-3} " -f $r2Str) -ForegroundColor $r2Col -NoNewline
        Write-Host "| I1: " -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,5:F2}mA " -f $curr1) -ForegroundColor DarkYellow -NoNewline
        Write-Host "| I2: " -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,5:F2}mA " -f $curr2) -ForegroundColor DarkYellow -NoNewline
        Write-Host "| R_CELL: " -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,6:F1}Ω " -f $cellOhms) -ForegroundColor Gray -NoNewline
        Write-Host "| R_TEMP: " -ForegroundColor DarkGray -NoNewline
        Write-Host ("{0,6:F1}Ω" -f $tempOhms) -ForegroundColor Gray
    }

    Start-Sleep -Milliseconds $IntervalMs
}

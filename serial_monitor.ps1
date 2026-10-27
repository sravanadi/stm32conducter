<#
.SYNOPSIS
    Real-Time Terminal Serial Monitor for Mirrant TDS 4002 (STM32G030K8T6)
    Uses native Windows .NET System.IO.Ports (Zero external dependencies).
#>

param(
    [string]$Port = "",
    [int]$Baud = 0
)

# Set UTF-8 Output Encoding
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

function Show-Header {
    param($activePort, $activeBaud)
    Clear-Host
    Write-Host "====================================================================================" -ForegroundColor Cyan
    Write-Host "             MIRRANT TDS 4002 - REAL-TIME SERIAL TELEMETRY MONITOR                   " -ForegroundColor White -BackgroundColor DarkBlue
    Write-Host "====================================================================================" -ForegroundColor Cyan
    Write-Host " Hardware : STM32G030K8T6 | PB6 = TX, PB7 = RX, PB5 = RS-485 DE/RE                 " -ForegroundColor Gray
    Write-Host " Header   : J3 Header Pin 4 = Common GND (Pin 1 = 3.3V)                             " -ForegroundColor Gray
    Write-Host " Port     : $activePort @ $activeBaud bps, 8 Data bits, No Parity, 1 Stop bit       " -ForegroundColor Yellow
    Write-Host " Controls : [H] Help  [D] Read  [S] Stream  [J] JSON  [F] Rate  [M] Math  [Q] Exit  " -ForegroundColor Green
    Write-Host "            [1] Toggle Relay 1  [2] Toggle Relay 2  [C] Print Active Config         " -ForegroundColor Green
    Write-Host "====================================================================================" -ForegroundColor Cyan
    Write-Host ""
}

# 1. Discover COM ports
$availablePorts = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object -Unique

if (-not $Port) {
    if ($availablePorts.Count -eq 0) {
        Write-Host "[!] No active COM ports detected." -ForegroundColor Red
        Write-Host "    Please connect your USB-to-RS485 or USB-to-UART adapter to the PC." -ForegroundColor Yellow
        $Port = Read-Host "`nEnter COM port manually (e.g. COM3)"
    } elseif ($availablePorts.Count -eq 1) {
        $Port = $availablePorts[0]
        Write-Host "[*] Auto-detected single COM port: $Port" -ForegroundColor Green
    } else {
        Write-Host "Detected Available COM Ports:" -ForegroundColor Cyan
        for ($i = 0; $i -lt $availablePorts.Count; $i++) {
            $pName = $availablePorts[$i]
            $desc = ""
            try {
                $dev = Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -like "*($pName)*" } | Select-Object -First 1
                if ($dev) { $desc = " - " + $dev.Name }
            } catch {}
            Write-Host "  [$($i+1)] $pName$desc" -ForegroundColor Yellow
        }
        Write-Host ""
        $selection = Read-Host "Select port [1-$($availablePorts.Count)] or enter COM name (Default: 1)"
        if ($selection -match '^\d+$' -and [int]$selection -ge 1 -and [int]$selection -le $availablePorts.Count) {
            $Port = $availablePorts[[int]$selection - 1]
        } elseif ($selection) {
            $Port = $selection.ToUpper().Trim()
        } else {
            $Port = $availablePorts[0]
        }
    }
}

$Port = $Port.ToUpper().Trim()
if (-not ($Port -match '^COM\d+$')) {
    Write-Host "[X] Invalid port format: '$Port'. Expected COMx (e.g. COM3)" -ForegroundColor Red
    pause
    exit 1
}

# 2. Select Baud Rate
if ($Baud -le 0) {
    Write-Host ""
    Write-Host "Select Baud Rate for Mirrant RS-485 / UART:" -ForegroundColor Cyan
    Write-Host "  [1] 9600 bps   (Default - Mirrant Factory Default)" -ForegroundColor Yellow
    Write-Host "  [2] 19200 bps" -ForegroundColor Yellow
    Write-Host "  [3] 38400 bps" -ForegroundColor Yellow
    Write-Host "  [4] 57600 bps" -ForegroundColor Yellow
    Write-Host "  [5] 115200 bps" -ForegroundColor Yellow
    Write-Host "  [C] Custom Baud Rate" -ForegroundColor Yellow
    $bChoice = Read-Host "`nSelect baud rate [1-5 or C] (Default: 1 - 9600 bps)"
    switch ($bChoice) {
        "1" { $Baud = 9600 }
        "2" { $Baud = 19200 }
        "3" { $Baud = 38400 }
        "4" { $Baud = 57600 }
        "5" { $Baud = 115200 }
        "c" {
            $cVal = Read-Host "Enter custom baud rate"
            if ($cVal -match '^\d+$') { $Baud = [int]$cVal } else { $Baud = 9600 }
        }
        "C" {
            $cVal = Read-Host "Enter custom baud rate"
            if ($cVal -match '^\d+$') { $Baud = [int]$cVal } else { $Baud = 9600 }
        }
        default {
            if ($bChoice -match '^\d{4,6}$') {
                $Baud = [int]$bChoice
            } else {
                $Baud = 9600
            }
        }
    }
}

# 3. Open Serial Port
$serial = New-Object System.IO.Ports.SerialPort
$serial.PortName = $Port
$serial.BaudRate = $Baud
$serial.Parity   = [System.IO.Ports.Parity]::None
$serial.DataBits = 8
$serial.StopBits = [System.IO.Ports.StopBits]::One
$serial.ReadTimeout  = 50
$serial.WriteTimeout = 500

try {
    $serial.Open()
} catch {
    Write-Host "`n[ERROR] Failed to open port $Port!" -ForegroundColor Red
    Write-Host "        Reason: $($_.Exception.Message)" -ForegroundColor Yellow
    Write-Host "        Check if another program (e.g. PuTTY, Arduino IDE) is using $Port.`n" -ForegroundColor Gray
    pause
    exit 1
}

Show-Header -activePort $Port -activeBaud $Baud
Write-Host "[OK] Connected to $Port @ $Baud bps. Fast live telemetry stream active...`n" -ForegroundColor Green

# Discard stale buffer
$serial.DiscardInBuffer()

# 4. Main Reading and Key Listening Loop
$rxBuffer = ""

try {
    while ($serial.IsOpen) {
        # Check for keyboard input
        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            $char = $key.KeyChar

            if ($key.Key -eq [ConsoleKey]::Escape -or $char -eq 'q' -or $char -eq 'Q') {
                Write-Host "`n[*] Exiting serial monitor..." -ForegroundColor Yellow
                break
            }

            if ($char) {
                try {
                    $serial.Write($char.ToString())
                    $serial.BaseStream.Flush()
                    Write-Host "[KEY SENT: '$char']" -ForegroundColor Magenta
                } catch {
                    Write-Host "[!] Write error: $($_.Exception.Message)" -ForegroundColor Red
                }
            }
        }

        # Check for incoming serial data (Non-blocking chunk read)
        if ($serial.BytesToRead -gt 0) {
            try {
                $chunk = $serial.ReadExisting()
                if ($chunk.Length -gt 0) {
                    $rxBuffer += $chunk

                    while ($rxBuffer.Contains("`n")) {
                        $newlineIdx = $rxBuffer.IndexOf("`n")
                        $line = $rxBuffer.Substring(0, $newlineIdx).TrimEnd("`r")
                        $rxBuffer = $rxBuffer.Substring($newlineIdx + 1)

                        if ($line.Length -gt 0) {
                            $ts = (Get-Date).ToString("HH:mm:ss.fff")

                            # Highlight formatted telemetry
                            if ($line -match '^\[FEED\]') {
                                Write-Host "[$ts] " -ForegroundColor DarkGray -NoNewline
                                
                                # Colorize fields
                                $parts = $line.Replace("[FEED] ", "").Split("|")
                                foreach ($part in $parts) {
                                    $trimmed = $part.Trim()
                                    if ($trimmed -match 'COND:([0-9\.\-]+)') {
                                        Write-Host "COND: " -ForegroundColor DarkCyan -NoNewline
                                        Write-Host "$($Matches[1]) uS/cm " -ForegroundColor Green -NoNewline
                                    } elseif ($trimmed -match 'RAW:([0-9\.\-]+)') {
                                        Write-Host "| RAW: " -ForegroundColor DarkGray -NoNewline
                                        Write-Host "$($Matches[1]) " -ForegroundColor DarkGreen -NoNewline
                                    } elseif ($trimmed -match 'TEMP:([0-9\.\-]+)\s*([CF])?') {
                                        $u = if ($Matches[2]) { "°$($Matches[2])" } else { "°C" }
                                        Write-Host "| TEMP: " -ForegroundColor DarkGray -NoNewline
                                        Write-Host "$($Matches[1]) $u " -ForegroundColor Cyan -NoNewline
                                    } elseif ($trimmed -match 'TDS:([0-9\.\-]+)') {
                                        Write-Host "| TDS: " -ForegroundColor DarkGray -NoNewline
                                        Write-Host "$($Matches[1]) ppm " -ForegroundColor Yellow -NoNewline
                                    } elseif ($trimmed -match 'RES:([0-9\.\-]+)') {
                                        Write-Host "| RES: " -ForegroundColor DarkGray -NoNewline
                                        Write-Host "$($Matches[1]) MΩ " -ForegroundColor Magenta -NoNewline
                                    } elseif ($trimmed -match 'SAL:([0-9\.\-]+)') {
                                        Write-Host "| SAL: " -ForegroundColor DarkGray -NoNewline
                                        Write-Host "$($Matches[1]) ppt " -ForegroundColor Blue -NoNewline
                                    } elseif ($trimmed -match 'R1:(ON|OFF)') {
                                        $rColor = if ($Matches[1] -eq "ON") { "Green" } else { "DarkGray" }
                                        Write-Host "| R1: " -ForegroundColor DarkGray -NoNewline
                                        Write-Host "$($Matches[1]) " -ForegroundColor $rColor -NoNewline
                                    } elseif ($trimmed -match 'R2:(ON|OFF)') {
                                        $rColor = if ($Matches[1] -eq "ON") { "Green" } else { "DarkGray" }
                                        Write-Host "| R2: " -ForegroundColor DarkGray -NoNewline
                                        Write-Host "$($Matches[1]) " -ForegroundColor $rColor -NoNewline
                                    } elseif ($trimmed -match 'I1:([0-9\.\-]+mA)') {
                                        Write-Host "| I1: " -ForegroundColor DarkGray -NoNewline
                                        Write-Host "$($Matches[1]) " -ForegroundColor DarkYellow -NoNewline
                                    } elseif ($trimmed -match 'I2:([0-9\.\-]+mA)') {
                                        Write-Host "| I2: " -ForegroundColor DarkGray -NoNewline
                                        Write-Host "$($Matches[1]) " -ForegroundColor DarkYellow -NoNewline
                                    } elseif ($trimmed -match 'R_CELL:([0-9\.\-]+)') {
                                        Write-Host "| R_CELL: " -ForegroundColor DarkGray -NoNewline
                                        Write-Host "$($Matches[1]) Ω " -ForegroundColor Gray -NoNewline
                                    } elseif ($trimmed -match 'R_TEMP:([0-9\.\-]+)') {
                                        Write-Host "| R_TEMP: " -ForegroundColor DarkGray -NoNewline
                                        Write-Host "$($Matches[1]) Ω " -ForegroundColor Gray -NoNewline
                                    } elseif ($trimmed -match '^\[K:') {
                                        Write-Host "$trimmed " -ForegroundColor DarkCyan -NoNewline
                                    } else {
                                        Write-Host "| $trimmed " -ForegroundColor Gray -NoNewline
                                    }
                                }
                                Write-Host ""
                            } elseif ($line.StartsWith("{") -and $line.EndsWith("}")) {
                                # JSON line
                                Write-Host "[$ts] JSON: " -ForegroundColor DarkYellow -NoNewline
                                Write-Host $line -ForegroundColor White
                            } elseif ($line -match '\[SETTING CHANGED\]') {
                                Write-Host "`n[$ts] " -ForegroundColor DarkGray -NoNewline
                                Write-Host $line -ForegroundColor Yellow -BackgroundColor DarkBlue
                            } elseif ($line -match '\[RELAY EVENT\]') {
                                Write-Host "`n[$ts] " -ForegroundColor DarkGray -NoNewline
                                Write-Host $line -ForegroundColor White -BackgroundColor DarkGreen
                            } elseif ($line -match '^\[MONITOR\]') {
                                Write-Host "[$ts] " -ForegroundColor DarkGray -NoNewline
                                Write-Host $line -ForegroundColor Yellow
                            } else {
                                # Standard text banner / help / formulas
                                if ($line -match 'CALCULATION|FORMULA|STEP-BY-STEP|Step [0-9]|OUTPUT|INPUT|VALUES') {
                                    Write-Host $line -ForegroundColor Cyan
                                } else {
                                    Write-Host $line -ForegroundColor Gray
                                }
                            }
                        }
                    }
                }
            } catch {
                # Ignore transient read glitches
            }
        }

        Start-Sleep -Milliseconds 2
    }
} finally {
    if ($serial -and $serial.IsOpen) {
        $serial.Close()
        Write-Host "[OK] Port $Port closed cleanly.`n" -ForegroundColor Green
    }
}

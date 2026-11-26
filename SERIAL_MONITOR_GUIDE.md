# Mirrant TDS 4002 - Real-Time Telemetry & Serial Monitor Guide

## 1. Quick Start: Which Monitor Should You Use?

You have two powerful live monitoring tools:

| Tool | Connection Method | Extra Wires Needed | Best Used For |
|:---|:---|:---:|:---|
| **[`swd_monitor.bat`](file:///c:/Users/srava/Documents/smt32/swd_monitor.bat)** | **Direct via ST-LINK on J3 Header** | **NONE (Zero wires!)** | **Immediate live monitoring right now** using your existing ST-LINK programmer connection. |
| **[`serial_monitor.bat`](file:///c:/Users/srava/Documents/smt32/serial_monitor.bat)** | **RS-485 Bus (Terminals A & B) or UART (PB6/PB7)** | USB-to-RS485 adapter or 2 jumper wires | Industrial RS-485 network verification, PLC integration, Modbus testing, and remote control. |

---

## 2. Option 1: Live SWD Telemetry Monitor (`swd_monitor.bat`)

Since your **ST-LINK programmer is already connected to the J3 header** (SWDIO, SWCLK, GND, 3.3V), you can view live measurements in real time with **zero extra wires**:

Double-click:
```bat
swd_monitor.bat
```
*(or run `.\swd_monitor.bat` in Command Prompt / PowerShell)*.

### Live Output Example:
```text
[11:45:07.144] COND:  11261.9 uS/cm | TURB:   12.4 NTU | TEMP:  41.4 °C | TDS:  5630.9 ppm | RES:   0.000 MΩ | SAL:  5.63 ppt | R1: ON  | R2: OFF | I1: 20.00mA | I2: 10.63mA
```
- Reads the MCU's calculation structure (`s_reading`) directly from RAM over SWD without halting firmware execution (auto-locating addresses from `build/display.map`).
- Press **`q`** or **`Ctrl+C`** to exit.

---

## 3. Option 2: Serial Port Monitor (`serial_monitor.bat`)

### Why did `COM5` not show data when you sent keys?
In your terminal, you connected to:
`COM5 - STMicroelectronics STLink Virtual COM Port (COM5)`

`COM5` is the Virtual COM Port built into your **NUCLEO programmer board**.
- The 5-pin ribbon cable from the ST-LINK to the target board's **J3 header** carries **SWD signals only** (`SWDIO`, `SWCLK`, `GND`, `3.3V`, `NRST`).
- **J3 DOES NOT have UART lines.**
- The MCU board's serial port (USART1) is on **PB6 (TX)** and **PB7 (RX)**, which are routed to the **THVD1450 RS-485 transceiver** (`U4`) and out to **Terminals A & B**.
- Therefore, `COM5` on the Nucleo board is not physically connected to PB6 and PB7!

### How to Connect for `serial_monitor.bat`:

#### Wiring Method A: Using a USB-to-RS485 Adapter (Recommended)
1. Connect USB Adapter **`A`** (`D+`) to Board Terminal **`A`** (RS-485 Signal +)
2. Connect USB Adapter **`B`** (`D-`) to Board Terminal **`B`** (RS-485 Signal -)
3. Connect USB Adapter **`GND`** to **J3 Header Pin 4 (`GND`)**
4. Plug into PC -> A new COM port appears (e.g. `COM6` or `COM7`).
5. Run `.\serial_monitor.bat` and select that new COM port.

#### Wiring Method B: Jumper Wires from ST-LINK Nucleo (`COM5`) to Target Board
If you want to use `COM5` without buying an RS-485 adapter:
1. On your Nucleo-F302R8 board, locate connector **`CN3`** (or Morpho pins `D0`/`D1` for ST-LINK VCP UART):
   - Connect **Nucleo STLK_RX**  -->  Target Board **`PB6`** (USART1 TX)
   - Connect **Nucleo STLK_TX**  -->  Target Board **`PB7`** (USART1 RX)
   - Connect **Nucleo GND**      -->  Target Board **J3 Header Pin 4 (`GND`)**
2. Run `.\serial_monitor.bat` and select `COM5`.

---

## 4. Interactive Keyboard Commands in `serial_monitor.bat`

When connected to the active serial port, press any of these keys directly in the terminal:

| Key | Action | Description |
|:---:|:---|:---|
| **`h`** / **`?`** | Help Menu | Displays the command summary |
| **`d`** / **`r`** | Read Snapshot | Instantly requests a single reading snapshot |
| **`m`** | Math Breakdown | Prints live step-by-step mathematical calculations with inputs & outputs |
| **`s`** | Toggle Streaming | Turns periodic live telemetry streaming ON / OFF |
| **`j`** | Toggle JSON Mode | Switches between formatted text and JSON |
| **`f`** | Toggle Rate | Switches between FAST (200 ms / 5 Hz), 100 ms, 500 ms, 1000 ms |
| **`1`** | Toggle Relay 1 | Overrides Relay 1: Force ON -> Force OFF -> Auto (updates LCD & serial) |
| **`2`** | Toggle Relay 2 | Overrides Relay 2: Force ON -> Force OFF -> Auto (updates LCD & serial) |
| **`c`** | View Config | Prints active configuration: K, TC, TDS_F, T_CAL, EC_FAC, EC_OFF, Unit |
| **`v`** | Version Info | Prints firmware version and hardware pinout |
| **`q`** / **`Esc`** | Exit | Closes port and exits cleanly |

---

## 5. Live Telemetry & Event Features

### 5.1 Real-Time Telemetry Feed (`[FEED]`)
Every 200 ms (or configured interval), the monitor broadcasts:
```text
[FEED] COND:1413.0 uS/cm | RAW:1413.0 | TEMP:25.0 C | TDS:706.5 ppm | RES:0.001 M-Ohm | SAL:0.71 ppt | R1:OFF | R2:OFF | I1:4.00mA | I2:4.00mA | R_CELL:707.7 | R_TEMP:10000.0 [K:1.000 TC:2.00% TDS_F:0.500 T_CAL:0.0 EC_FAC:1.000 EC_OFF:0.0 UNIT:C REAL]
```

### 5.2 Automatic Device Settings Change Detection (`[SETTING CHANGED]`)
Whenever any setting is modified on the meter (via buttons/menu or serial/Modbus), the monitor instantly alerts:
```text
************************************************************************
>>> [SETTING CHANGED] Active Measurement Parameters Updated by User <<<
************************************************************************
  Cell Constant K  : 1.000 -> 1.050 (1/cm)
  Probe Cal        : Fac 1.000 -> 1.025 | Off 0.0 -> 2.5 uS (Pts: 0->1)
  Temp Coeff (TC)  : 2.00 -> 2.10 %/C
  Temp Cal Offset  : 0.0 -> 0.5 C
  Temp Unit        : Celsius (C) -> Fahrenheit (F)
  TDS Factor       : 0.500 -> 0.650
  Relay 1 Updated  : Func 0 (ON: 2000.0, OFF: 1800.0)
  Relay 2 Updated  : Func 1 (ON: 35.0, OFF: 30.0)
------------------------------------------------------------------------
```
Followed immediately by the full step-by-step mathematical calculations showing how the new parameters recalculate the outputs.

### 5.3 Live Mathematical Calculation Engine (`'m'` key or on Setting Change)
```text
========================================================================
       MIRRANT TDS 4002 - LIVE MATHEMATICAL CALCULATION BREAKDOWN
========================================================================
[1. INPUT MEASUREMENT VALUES]
  Raw Cell Resistance (R_cell) : 707.7 Ohms
  Temp Sensor Resistance(R_temp): 10000.0 Ohms (Type: NTC10K)
  Cell Constant (K)            : 1.000 1/cm
  Temp Compensation Coeff (TC) : 2.00 %/C (Ref: 25.0 C)
  Temp Calibration Offset      : 0.0 C (Unit: C)
  Probe Cal Factor / Offset    : Factor=1.000 | Offset=0.0 uS/cm (Pts: 0)
  TDS Conversion Factor (TDS_F): 0.500
------------------------------------------------------------------------
[2. STEP-BY-STEP MATHEMATICAL CALCULATIONS]
  Step 1: Temperature with Offset Calibration:
    T_meas = 25.0 C
    Temp_C = T_meas + Temp_Cal_Offset = 25.0 + 0.0 = 25.0 C
    Temp_F = (Temp_C * 1.8) + 32.0 = 77.0 F
  Step 2: Raw Conductivity (Uncompensated EC):
    EC_raw = (K * 1,000,000) / R_cell
           = (1.000 * 1,000,000) / 707.7
           = 1413.0 uS/cm
  Step 3: Temperature Compensation to 25.0 C Standard:
    Comp_Factor = 1.0 + (TC / 100.0) * (Temp_C - 25.0)
                = 1.0 + (2.00 / 100.0) * (25.0 - 25.0) = 1.000
    EC_25       = EC_raw / Comp_Factor = 1413.0 / 1.000 = 1413.0 uS/cm
  Step 4: Probe Calibration Correction:
    EC_cal      = (EC_25 * EC_Factor) + EC_Offset
                = (1413.0 * 1.000) + 0.0 = 1413.0 uS/cm
  Step 5: Total Dissolved Solids (TDS):
    TDS (ppm)   = EC_cal * TDS_Factor = 1413.0 * 0.500 = 706.5 ppm
  Step 6: Resistivity (RES) & Salinity (SAL):
    RES (M-Ohm) = 1.0 / EC_cal = 0.001 M-Ohm*cm (Capped: 18.0)
    SAL (ppt)   = (EC_cal / 1000.0) * 0.50 = 0.71 ppt
  Step 7: Analog Loops (4-20mA) & Relay Alarms:
    Current I1  = 4.00 mA | Current I2 = 4.00 mA
    Relay 1     : Func 0 (ON: 2000.0, OFF: 1800.0) -> State: DE-ENERGIZED (OFF)
    Relay 2     : Func 1 (ON: 35.0, OFF: 30.0) -> State: DE-ENERGIZED (OFF)
------------------------------------------------------------------------
[3. OUTPUT DISPLAY VALUES (Matches LCD Screen)]
  COND: 1413.0 uS/cm | RAW: 1413.0 | TEMP: 25.0 C | TDS: 706.5 ppm
  RES : 0.001 M-Ohm | SAL: 0.71 ppt | R1: OFF | R2: OFF
========================================================================
```

### 5.4 Real-Time Relay Synchronization (`[RELAY EVENT]`)
Whenever Relay 1 or Relay 2 triggers (via measurement thresholds, UI manual test, or serial keys `'1'` / `'2'`), the state change immediately updates the physical LCD screen and serial monitor:
```text
>>> [RELAY EVENT] Relay 1: OFF -> ON | Meas: 2050.0 uS/cm | On: 2000.0 | Off: 1800.0 <<<
```

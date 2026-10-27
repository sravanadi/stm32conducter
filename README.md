# Mirrant TDS 4002 - STM32 Conductivity & Process Controller Firmware

Firmware and tooling for the **Mirrant TDS 4002** Online Conductivity, Resistivity, TDS, and Salinity Process Controller.

- **Target MCU:** STMicroelectronics STM32G030K8T6 (Arm Cortex-M0+ @ 16 MHz HSI)
- **Display:** 3.2-inch Graphic LCD (128x64 ST7565 controller over SPI1)
- **Probe Interface:** 2-Electrode AC Bipolar Excitation with Analog Front-End (AFE) Daughterboard (Header J2)
- **Temperature Probes:** NTC10K thermistor / PT1000 RTD with automatic/manual temperature compensation
- **Communications:** Industrial RS-485 (USART1 on PB6/PB7 with PB5 RE/DE direction control), Modbus RTU Slave (FC 0x04)
- **Control Outputs:** 2x SPST Process Alarm/Wash Relays (PA3, PA4) and 2x Active 4-20mA / 0-20mA Current Loops

---

## Features

1. **5-in-1 Process Measurement Engine:**
   - **Conductivity (COND):** $0.000 \sim 20.000\,\text{mS/cm}$ ($0 \sim 20000\,\mu\text{S/cm}$) with 25°C temperature compensation.
   - **Resistivity (RES):** $0.001 \sim 18.000\,\text{M}\Omega\cdot\text{cm}$.
   - **Total Dissolved Solids (TDS):** $0 \sim 10000\,\text{ppm}$ with configurable factor.
   - **Salinity (SAL):** $0.000 \sim 15.000\,\text{ppt}$.
   - **Sample Test (FVAL):** Real-time evaluation of custom formula $(a+b)(c-d)$.

2. **Electrochemical Anti-Polarization Engine:**
   - Continuous high-frequency bipolar square-wave excitation on pins PA6 & PA7 clears dielectric plate charges and eliminates electrode electrolysis in liquid samples.

3. **Dual Industrial Monitoring & Diagnostics:**
   - **RS-485 Modbus RTU & ASCII Diagnostics:** Interactive console, Modbus register queries (FC 0x04), and full step-by-step mathematical breakdown stream via [`test_rs485.bat`](test_rs485.bat) and [`test_rs485.py`](test_rs485.py).
   - **Live SWD Telemetry Monitor:** Real-time RAM telemetry directly over the J3 ST-LINK header with zero extra wires via [`swd_monitor.bat`](swd_monitor.bat) and [`swd_monitor.ps1`](swd_monitor.ps1).

4. **Non-Volatile Flash Persistence:**
   - Flash Page 31 configuration storage with CRC-16 Modbus integrity validation, parameter sanitization, and wear prevention.

---

## Hardware Pinout Summary

| Pin | Function | Peripheral / Description |
| :--- | :--- | :--- |
| **PA0** | `CELL_SENSE` | ADC1_IN0, Conductivity probe divider voltage input |
| **PA1** | `LCD_SCK` | SPI1 Clock (Display) |
| **PA2** | `LCD_MOSI` | SPI1 Data (Display) |
| **PA3** | `RELAY_1` | ISO1 Optocoupler driver for Relay 1 |
| **PA4** | `RELAY_2` | ISO2 Optocoupler driver for Relay 2 |
| **PA5** | `TEMP_SENSE` | ADC1_IN5, NTC10K / PT1000 temperature divider input |
| **PA6** | `EXC_A` | Bipolar AC Excitation Phase A (Push-Pull) |
| **PA7** | `EXC_B` | Bipolar AC Excitation Phase B (Push-Pull) |
| **PA8** | `LCD_CS` | Display Chip Select |
| **PA9** | `LCD_RES` | Display Hardware Reset |
| **PA10**| `LCD_A0` | Display Register / Command Select |
| **PB5** | `RS485_DIR` | THVD1450 RS-485 Transceiver RE/DE direction control |
| **PB6** | `USART1_TX` | RS-485 Serial Transmit |
| **PB7** | `USART1_RX` | RS-485 Serial Receive |
| **PC6** | `LCD_LED` | Display Backlight Control |
| **J3 Header** | `SWD` | ST-LINK Programming & Debug (SWDIO, SWCLK, GND, 3.3V) |

---

## Building and Flashing

### Requirements
- ARM GNU Toolchain (`arm-none-eabi-gcc`)
- STM32CubeProgrammer CLI (for flashing over ST-LINK)
- Python 3 with `pyserial` (for RS-485 testing)

### Build Firmware
Run in PowerShell or Command Prompt:
```powershell
.\build.ps1
```
*(or double-click [`build.bat`](build.bat))*.

The compiler produces:
- `build/display.elf`
- `build/display.hex`
- `build/display.bin`

### Flash to Target
Connect ST-LINK to the J3 header and run:
```powershell
.\flash.ps1
```
*(or double-click [`flash.bat`](flash.bat))*.

---

## Documentation Guides

- **[`SENSOR_CONNECTION_GUIDE.md`](SENSOR_CONNECTION_GUIDE.md):** Complete wiring guide for the AFE daughterboard header J2 and rear screw terminals.
- **[`SERIAL_MONITOR_GUIDE.md`](SERIAL_MONITOR_GUIDE.md):** Guide for live telemetry over RS-485 and direct SWD RAM streaming.

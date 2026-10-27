# Mirrant TDS 4002 - Sensor Probe & Daughterboard Hardware Connection Guide

This document explains how the **Sensor Probe**, **AFE Daughterboard**, and **STM32 Mainboard** physically connect, how they transfer data, and how real-time measurements operate.

---

## 1. Quick Summary (Answers to Your Questions)

| Question | Answer |
| :--- | :--- |
| **Where does the probe connect?** | The sensor cable connects to the rear screw terminal block: **CELL**, **CELL**, **TEMP**, and **TEMP**. The Daughterboard connects directly to the 8-pin **J2 Header** on the STM32 mainboard. |
| **How do they transfer data?** | It is an **active Analog Front-End (AFE)** interface (not serial or digital). The STM32 sends a **bipolar AC excitation wave** on **PA6/PA7** and reads back the solution voltage via 12-bit ADC on **PA0** and temperature on **PA5** every 100 ms. |
| **Does it show real-time feed?** | **YES! 100% automatically.** As soon as the probe is immersed in water, live measurements appear immediately on the LCD screen, over RS-485, and in the SWD monitor. |
| **Should I change the code after connection?** | **NO! Zero code changes needed.** The firmware is already programmed to automatically detect the probe, apply temperature compensation, and compute all measurements dynamically. |

---

## 2. Hardware Architecture & Wiring Diagram

```
 [ Conductivity & Temp Sensor Probe ]
     │  (Immersed in solution)
     │  4-Core Shielded Cable
     ▼
 ┌──────────────────────────────────────────────────────────────┐
 │             REAR SCREW TERMINAL BLOCK (BACK COVER)           │
 │                                                              │
 │   [ CELL ]    ─── Electrode Wire 1 (Conductivity)            │
 │   [ CELL ]    ─── Electrode Wire 2 (Conductivity)            │
 │   [ TEMP ]    ─── Temperature Wire 1 (NTC10K or PT1000)      │
 │   [ TEMP ]    ─── Temperature Wire 2 (Ground / Return)       │
 │   [ SHIELD ]  ─── Outer Cable Braided Shield (Earth/Noise)   │
 └──────────────────────────────┬───────────────────────────────┘
                                │ Internal Traces
                                ▼
 ┌──────────────────────────────────────────────────────────────┐
 │              PROBE AFE DAUGHTERBOARD MODULE                  │
 │   - Bipolar AC excitation drive bridge                       │
 │   - High-precision series reference resistors (0.1%)         │
 │   - Analog filters and voltage clamping protection           │
 └──────────────────────────────┬───────────────────────────────┘
                                │ 8-Pin 2.54mm Female Header
                                ▼
 ┌──────────────────────────────────────────────────────────────┐
 │               HEADER J2 (STM32 MAINBOARD)                    │
 │                                                              │
 │   Pin 1 : +3.3V Analog Power Supply                          │
 │   Pin 2 : GND (Analog Ground Reference)                      │
 │   Pin 3 : EXC_A (PA6, MCU Pin 13) - AC Excitation Phase A    │
 │   Pin 4 : EXC_B (PA7, MCU Pin 14) - AC Excitation Phase B    │
 │   Pin 5 : CELL_SENSE (PA0, MCU Pin 7, ADC1_IN0)              │
 │   Pin 6 : TEMP_SENSE (PA5, MCU Pin 12, ADC1_IN5)             │
 │   Pin 7 : NC / SPARE (PA15)                                  │
 │   Pin 8 : SHIELD (Connected to chassis ground)               │
 └──────────────────────────────────────────────────────────────┘
```

---

## 3. Rear Terminal Block Wiring Details

When connecting your physical sensor cable to the meter's rear terminals:

| Terminal Name | Cable Wire Function | Typical Wire Colors | Description |
| :--- | :--- | :--- | :--- |
| **CELL** | Conductivity Electrode 1 | Red or Black | Excitation current from daughterboard into the liquid |
| **CELL** | Conductivity Electrode 2 | Yellow or White | Sensed voltage return from the liquid |
| **TEMP** | Temperature Sensor (+) | Green or Blue | Connected to internal NTC10K thermistor or PT1000 RTD |
| **TEMP** | Temperature Sensor (-) | Black or Brown | Temperature sensor ground reference |
| **SHIELD** | Cable Shielding | Bare copper braid | Prevents 50Hz/60Hz AC noise interference from pumps/motors |

*(Note: Conductivity is non-polarized AC, so the two CELL wires can be connected in either order. For 2-wire temperature sensors, the two TEMP wires can also be connected in either order).*

---

## 4. How the Board and Daughterboard Communicate

The daughterboard is not a digital microprocessor; it is an **Analog Front End (AFE)**. The STM32 communicates with it using direct analog hardware peripherals:

1. **Anti-Polarization Bipolar AC Excitation (PA6 & PA7):**
   - Water electrolysis happens if you apply constant DC voltage to water (bubbles form and electrodes corrode).
   - To prevent this, the STM32 generates high-frequency **bipolar alternating square waves** across pins **PA6 (EXC_A)** and **PA7 (EXC_B)**.
   - First half-cycle: PA6 is +3.3V, PA7 is 0V.
   - Second half-cycle: PA6 is 0V, PA7 is +3.3V (instantly clearing dielectric charge).

2. **Liquid Cell Sensing (PA0 / ADC1_IN0):**
   - The solution's electrical resistance forms a precision voltage divider with the daughterboard's internal reference resistor ($R_{\text{ref}} = 1000\,\Omega$).
   - The STM32's 12-bit ADC samples the resulting voltage 16 times, removes noise, and calculates:
     $$R_{\text{cell}} = R_{\text{ref}} \times \frac{\text{ADC}}{4095 - \text{ADC}}$$

3. **Temperature Sensing (PA5 / ADC1_IN5):**
   - Pin PA5 reads the voltage across the temperature sensor (NTC10K or PT1000).
   - The STM32 converts the measured ohms into exact Celsius temperature using the Steinhart-Hart equation or IEC 60751 formula.

4. **Update Rate:**
   - This hardware cycle runs continuously in the background every **100 milliseconds (10 times every second)**.

---

## 5. Real-Time Feed Pipeline

Once the sensor probe is placed into liquid, the firmware immediately processes the readings through the 5-step engine:

```
  [Physical Water Sample]
            │
            ▼
    1. AFE Measures: R_cell (Ohms) & R_temp (Ohms)
            │
            ▼
    2. Temp Calculation: T (°C) from NTC10K / PT1000
            │
            ▼
    3. Raw Conductivity: EC_t = [ K x (1 / R_cell) x 1,000,000 ]
            │
            ▼
    4. Temperature Compensation to 25°C:
       Correction Factor = 1 + α x (T - 25)
       Compensated EC25  = EC_t / Correction Factor
            │
            ▼
    5. Derived Metrics:
       • Resistivity : ρ = 1 / EC25 (MΩ·cm)
       • TDS         : TDS = EC25 x TDS_Factor (ppm)
       • Salinity    : SAL = (EC25 / 1000) x 0.50 (ppt)
       • Sample Test : FVAL = (a + b) x (c - d)
       • Current 1   : I1 = 4..20 mA (e.g., mapped to EC)
       • Current 2   : I2 = 4..20 mA (e.g., mapped to Temp)
            │
            ├───────────────┬───────────────┐
            ▼               ▼               ▼
     [ Graphic LCD ]   [ RS-485 Bus ]   [ SWD Monitor ]
       Live 100ms       Modbus RTU &     RAM Live Feed
       Screen Feed      ASCII Telemetry  (swd_monitor.bat)
```

---

## 6. Do You Need to Change the Firmware?

**NO! You do NOT need to modify or recompile any C code.**

The firmware includes built-in state detection:
- **Probe in Air / Unplugged ($R > 10\,\text{M}\Omega$):** Shows zero conductivity cleanly without floating noise or random flicker.
- **Probe Immersed in Solution ($0.5\,\Omega < R < 10\,\text{M}\Omega$):** The live measurement engine automatically activates and streams live calculations.
- **Probe Short-Circuit ($R < 0.5\,\Omega$):** Displays `"SHORT"` fault protection.

### Configuration via Front Panel Buttons (No Code Changes):
The only thing you configure is your probe's physical specifications via the LCD menu:
1. **Cell Constant ($K$):** Check the label printed on your sensor (e.g. $K=1.000$, $K=0.100$, or $K=10.00$). Enter this under **Menu $\rightarrow$ Probe Setting $\rightarrow$ K Constant**.
2. **Temperature Sensor Type:** If your probe has an NTC 10K thermistor, leave it on `NTC10K` (default). If it has a PT1000 RTD, select `PT1000` under **Menu $\rightarrow$ Probe Setting $\rightarrow$ Temp Probe**.
3. **Save:** The settings are automatically written to STM32 non-volatile Flash and remembered permanently across power cycles.

---

## 7. How to Verify Real-Time Operation

1. **On the Front LCD Screen:**
   - Place the probe in water.
   - The large middle numbers will immediately display the real-time conductivity.
   - Press the **Down Arrow button** to see real-time temperature, $K$-constant, TDS, Salinity, and $I_1/I_2$ current loops.

2. **Over RS-485 (`test_rs485.bat`):**
   - Run `test_rs485.bat` $\rightarrow$ Option 2 (Interactive Console).
   - Type **`m`** to see the full live 5-step mathematical calculation with your probe's real-time $R_{\text{cell}}$ and $R_{\text{temp}}$.
   - Type **`d`** to see the single-line real-time reading.

3. **Over SWD Header J3 (`swd_monitor.bat`):**
   - With your ST-LINK connected to Header J3, run `.\swd_monitor.bat`.
   - Displays real-time readings directly from STM32 RAM every 200 ms with zero extra cables.

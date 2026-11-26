# Mirrant TDS 4002 Firmware Metrology Audit & Verification Report

**Document ID:** `AUDIT-TDS4002-2026-V1.0.5`  
**Date:** 07 November 2026  
**Target MCU:** STM32G030K8T6 (Arm Cortex-M0+, 16 MHz HSI, 12-bit ADC, $V_{DDA} = 3.3\text{ V}$)  
**Sensor Specification:** Passive 2-Electrode 316L Stainless Steel Cell

- Label Cell Constant: $K = 0.0852\text{ cm}^{-1}$ (Nominal $K = 0.1\text{ cm}^{-1}$)
- Operating Measurement Range: $0.5\text{ to }200.0\ \mu\text{S/cm}$ ($0\text{ to }100^\circ\text{C}$)
- Temperature Sensor: PT1000 RTD (IEC 60751 Class A/B)
- Excitation Topology: Passive resistor divider driven by microcontroller GPIOs

---

## 1. Executive Summary & Verdict Table

Each measurement concept and formula in the firmware was audited against physical circuit schematics, IEC 60751, Standard Methods 2510 B, and test vectors.

|  Item  | Concept / Specification                    |         Verdict         | Location in Source                                                                                                                                                                               | Description / Evidence                                                                                                                                                                                                                                            |
| :----: | :----------------------------------------- | :---------------------: | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **1**  | **Voltage Divider & ADC Formula**          |        **FAIL**         | [meter_afe.c:143](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_afe.c#L143)                                                                                                              | $R_{probe} = R_{ref} \times N / (4095 - N)$ matches low-side probe topology. However, single fixed $R_{ref} = 1000\ \Omega$ causes severe quantization error ($7418\ \Omega/\text{count} = 4.35\%/\text{LSB}$) at $0.5\ \mu\text{S/cm}$ ($170.4\text{ k}\Omega$). |
| **2**  | **Polarisation & Bipolar AC Drive**        |        **FAIL**         | [meter_afe.c:111-125](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_afe.c#L111-L125)                                                                                                     | Drives PA6 HIGH / PA7 LOW (sampled), then PA6 LOW / PA7 HIGH (**not sampled**). Negative half-cycle is never measured or averaged, failing to cancel DC electrochemical potential.                                                                                |
| **3**  | **Range Switching**                        |        **FAIL**         | [meter_afe.h:20](file:///c:/Users/srava/Documents/smt32/Core/Inc/meter_afe.h#L20)                                                                                                                | Only a single $1\text{ k}\Omega$ range resistor exists. There is no switching mechanism, rule, or hysteresis to cover $426\ \Omega$ to $170.4\text{ k}\Omega$.                                                                                                    |
| **4**  | **Conductivity ($EC_{25}$ Temp Comp)**     |        **PASS**         | [meter_core.c:291-299](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L291-L299)                                                                                                   | $EC_{raw} = \frac{K \cdot 10^6}{R_{cell}}$, $EC_{25} = \frac{EC_{raw}}{1 + (\alpha/100)(T - 25.0)}$. Exact mathematical match with zero-divide protection.                                                                                                        |
| **5**  | **Piecewise 1 to 5 Point Calibration**     |        **PASS**         | [meter_core.c:112-145](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L112-L145)<br>[meter_ui.c:2091](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_ui.c#L2091)            | Line through zero below Point 1, linear interpolation between points, slope extrapolation above last point. Low-to-high point ordering enforced in UI.                                                                                                            |
| **6**  | **Resistivity, TDS, Salinity**             | **PASS<br>_(Flagged)_** | [meter_core.c:340-358](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L340-L358)                                                                                                   | $RES = 1/EC$ capped at 18.0. $TDS = EC \times 0.50$. $SAL = (EC/1000) \times 0.50$. **FLAG:** Factor 0.50 is an empirical linear NaCl proxy; true seawater salinity requires PSS-78 polynomials.                                                                  |
| **7**  | **PT1000 RTD Conversion**                  |        **FAIL**         | [meter_core.c:109](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L109)<br>[meter_core.c:33](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L33)                     | 1) Code uses linear approximation $(R - 1000)/3.85$, causing $+0.29^\circ\text{C}$ error at $25^\circ\text{C}$ and $+0.38^\circ\text{C}$ error at $50^\circ\text{C}$.<br>2) Default probe type is set to `TEMP_PROBE_NTC10K` instead of `PT1000`.                 |
| **8**  | **Cell Constant ($K$) Menu Decimal Limit** |        **FAIL**         | [meter_ui.c:1590](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_ui.c#L1590)                                                                                                              | Menu editor format string `"%06.3f"` truncates $K = 0.0852$ to $0.085$, causing a $-0.235\%$ uncorrectable systematic scaling error.                                                                                                                              |
| **9**  | **Out-of-Range & Open Probe Handling**     |        **FAIL**         | [meter_core.c:229-246](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L229-L246)                                                                                                   | When cell is open in air ($R_{cell} > 10\text{ M}\Omega$), code **injects an invented benchmark reading** ($500\ \Omega$ at $30^\circ\text{C} = 1818.18\ \mu\text{S/cm}$) instead of declaring `fault_open`.                                                      |
| **10** | **Relays, 4-20 mA Outputs, Modbus RTU**    |        **PASS**         | [meter_core.c:164-215](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L164-L215)<br>[meter_core.c:434-537](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L434-L537) | Hysteresis handles both $ON > OFF$ and $ON < OFF$. Analog loops clamp to $4..20\text{ mA}$. Modbus FC04 generates compliant IEEE 754 CDAB byte-swapped floats.                                                                                                    |
| **11** | **Sample Test Formula ($\text{FVALUE}$)**  |        **PASS**         | [meter_core.c:252](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L252)                                                                                                            | Implements $\text{FVALUE} = (a + b) \times (c - d)$, mapped to Modbus registers 6–7.                                                                                                                                                                              |

---

## 2. Test Vector Verification Table

_Evaluation Conditions: $K = 0.0852\text{ cm}^{-1}$, $TC = 2.00\%/^\circ\text{C}$, $EC_{factor} = 1.000$, $EC_{offset} = 0.0$, $TDS_{coef} = 0.500$, $T = 25.0^\circ\text{C}$ unless specified._

| Test Domain                                      | Input Condition                       | Expected Result                       | Code Engine Output                    |     Absolute Error      | Relative Error |               Status                |
| :----------------------------------------------- | :------------------------------------ | :------------------------------------ | :------------------------------------ | :---------------------: | :------------: | :---------------------------------: |
| **Conductivity $\to$ Resistance**                | $EC = 0.5\ \mu\text{S/cm}$            | $R = 170400.0\ \Omega$                | $170400.0\ \Omega$                    |      $0.0\ \Omega$      |    $0.00\%$    |              **PASS**               |
|                                                  | $EC = 10.0\ \mu\text{S/cm}$           | $R = 8520.0\ \Omega$                  | $8520.0\ \Omega$                      |      $0.0\ \Omega$      |    $0.00\%$    |              **PASS**               |
|                                                  | $EC = 84.0\ \mu\text{S/cm}$           | $R = 1014.3\ \Omega$                  | $1014.29\ \Omega$                     |     $-0.01\ \Omega$     |   $-0.001\%$   |              **PASS**               |
|                                                  | $EC = 100.0\ \mu\text{S/cm}$          | $R = 852.0\ \Omega$                   | $852.0\ \Omega$                       |      $0.0\ \Omega$      |    $0.00\%$    |              **PASS**               |
|                                                  | $EC = 200.0\ \mu\text{S/cm}$          | $R = 426.0\ \Omega$                   | $426.0\ \Omega$                       |      $0.0\ \Omega$      |    $0.00\%$    |              **PASS**               |
| **Resistance $\to$ Outputs** ($R=852\ \Omega$)   | Conductivity                          | $100.00\ \mu\text{S/cm}$              | $100.00\ \mu\text{S/cm}$              | $0.00\ \mu\text{S/cm}$  |    $0.00\%$    |              **PASS**               |
|                                                  | Resistivity ($RES$)                   | $0.010\ \text{M}\Omega\cdot\text{cm}$ | $0.010\ \text{M}\Omega\cdot\text{cm}$ |         $0.000$         |    $0.00\%$    |              **PASS**               |
|                                                  | TDS                                   | $50.00\text{ ppm}$                    | $50.00\text{ ppm}$                    |    $0.00\text{ ppm}$    |    $0.00\%$    |              **PASS**               |
|                                                  | Salinity ($SAL$)                      | $0.050\text{ ppt}$                    | $0.050\text{ ppt}$                    |   $0.000\text{ ppt}$    |    $0.00\%$    |              **PASS**               |
| **Temperature Compensation**                     | $R=852\ \Omega, T=25.0^\circ\text{C}$ | $EC_{25} = 100.00\ \mu\text{S/cm}$    | $100.00\ \mu\text{S/cm}$              | $0.00\ \mu\text{S/cm}$  |    $0.00\%$    |              **PASS**               |
|                                                  | $R=852\ \Omega, T=35.0^\circ\text{C}$ | $EC_{25} = 83.333\ \mu\text{S/cm}$    | $83.333\ \mu\text{S/cm}$              | $0.000\ \mu\text{S/cm}$ |    $0.00\%$    |              **PASS**               |
| **PT1000 Resistance to Temp**                    | $R = 1000.00\ \Omega$                 | $T = 0.00^\circ\text{C}$              | $0.00^\circ\text{C}$                  |  $0.00^\circ\text{C}$   |    $0.00\%$    |              **PASS**               |
|                                                  | $R = 1097.35\ \Omega$                 | $T = 25.00^\circ\text{C}$             | **$25.29^\circ\text{C}$**             |  $+0.29^\circ\text{C}$  | **$+1.14\%$**  |              **FAIL**               |
|                                                  | $R = 1193.97\ \Omega$                 | $T = 50.00^\circ\text{C}$             | **$50.38^\circ\text{C}$**             |  $+0.38^\circ\text{C}$  | **$+0.76\%$**  |              **FAIL**               |
|                                                  | $R = 1385.05\ \Omega$                 | $T = 100.00^\circ\text{C}$            | $100.01^\circ\text{C}$                |  $+0.01^\circ\text{C}$  |   $+0.01\%$    |              **PASS**               |
| **ADC Quantization** ($R_{ref}=1\text{k}\Omega$) | $R = 426\ \Omega$                     | $N = 1223 \to 425.8\ \Omega$          | $425.84\ \Omega$                      |     $+0.04\ \Omega$     |   $+0.01\%$    |              **PASS**               |
|                                                  | $R = 852\ \Omega$                     | $N = 1884 \to 852.1\ \Omega$          | $852.10\ \Omega$                      |     $+0.10\ \Omega$     |   $+0.01\%$    |              **PASS**               |
|                                                  | $R = 1014.3\ \Omega$                  | $N = 2062 \to 1014.3\ \Omega$         | $1014.26\ \Omega$                     |     $-0.04\ \Omega$     |   $-0.004\%$   |              **PASS**               |
|                                                  | $R = 1097.35\ \Omega$                 | $N = 2142 \to 1096.8\ \Omega$         | $1096.77\ \Omega$                     |     $-0.58\ \Omega$     |   $-0.05\%$    |              **PASS**               |
|                                                  | $R = 170400\ \Omega$                  | $N = 4071 \to 169625\ \Omega$         | $169625.0\ \Omega$                    |    $-775.0\ \Omega$     | **$-0.45\%$**  | **FAIL** _(1 LSB = $7418\ \Omega$)_ |
| **Piecewise Calibration**                        | $EC_{25} = 50.0\ \mu\text{S/cm}$      | $42.00\ \mu\text{S/cm}$               | $42.00\ \mu\text{S/cm}$               | $0.00\ \mu\text{S/cm}$  |    $0.00\%$    |              **PASS**               |
| _(Std Points: $100\to 84$, $1000\to 1413$)_      | $EC_{25} = 550.0\ \mu\text{S/cm}$     | $748.50\ \mu\text{S/cm}$              | $748.50\ \mu\text{S/cm}$              | $0.00\ \mu\text{S/cm}$  |    $0.00\%$    |              **PASS**               |
|                                                  | $EC_{25} = 2000.0\ \mu\text{S/cm}$    | $2889.67\ \mu\text{S/cm}$             | $2889.67\ \mu\text{S/cm}$             | $0.00\ \mu\text{S/cm}$  |    $0.00\%$    |              **PASS**               |
| **Modbus Float (CDAB)**                          | $25.0^\circ\text{C}$                  | `00 00 41 C8`                         | `00 00 41 C8`                         |       Byte Match        |     $0.00      |
| \%$                                              | **PASS**                              |
|                                                  | $100.0\ \mu\text{S/cm}$               | `00 00 42 C8`                         | `00 00 42 C8`                         |       Byte Match        |    $0.00\%$    |              **PASS**               |
| **Sample Formula**                               | $a=3, b=2, c=5, d=1$                  | $20.00$                               | $20.00$                               |         $0.00$          |    $0.00\%$    |              **PASS**               |

---

## 3. Discovered Bugs & Targeted Remediation Plan

### Bug 1: Open Cell Condition Overwritten with Fake Bench Value

- **Severity:** **CRITICAL** (Metrology & Operational Safety Violation)
- **File:** [Core/Src/meter_core.c](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L229-L246)
- **Lines:** 229–246
- **Root Cause:** If the probe is in air or unplugged ($R_{cell} > 10\text{ M}\Omega$), the code forcefully sets:
  ```c
  cell_ohms = 500.0f;
  temp_ohms = 8038.0f;
  ```
  This causes the controller to report a fabricated reading of **$1818.18\ \mu\text{S/cm}$ at $30^\circ\text{C}$**.
- **Remediation Plan:** Delete lines 229–246. Set `out->fault_open = true`, zero all measurement metrics, and abort further calculation.

---

### Bug 2: Asymmetric Polarisation (Negative Half-Cycle Not Sampled)

- **Severity:** **HIGH** (Electrode Degradation & Uncompensated Offset)
- **File:** [Core/Src/meter_afe.c](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_afe.c#L111-L125)
- **Lines:** 111–125
- **Root Cause:** The bipolar excitation routine drives Phase A (`PA6=HIGH, PA7=LOW`) and reads `ADC_IN0`. It drives Phase B (`PA6=LOW, PA7=HIGH`) for $80\ \mu\text{s}$ but never reads the ADC. Any contact galvanic potential or asymmetric double-layer charge remains uncancelled.
- **Remediation Plan:** Sample PA0 during both phases, compute the differential peak-to-peak amplitude, and divide by 2 to reject DC electrochemical offsets.

---

### Bug 3: Severe Quantization Noise at Low Conductivity ($0.5\ \mu\text{S/cm}$)

- **Severity:** **HIGH** (Dynamic Range Mismatch)
- **File:** [Core/Src/meter_afe.c](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_afe.c#L10) and [meter_afe.h](file:///c:/Users/srava/Documents/smt32/Core/Inc/meter_afe.h#L20)
- **Lines:** Line 10 in `meter_afe.c`
- **Root Cause:** With a single $1\text{ k}\Omega$ reference resistor, $R_{cell} = 170.4\text{ k}\Omega$ yields $N = 4071$ counts. The cutoff for open-circuit is 4085 counts (only 14 counts margin), and each LSB step represents $7418\ \Omega$ ($4.35\%$ error per LSB).
- **Remediation Plan:**
  1. Software immediate fix: Move the open-circuit detection threshold to 4092 counts and increase ADC oversampling from 16x to 64x with a 16-bit accumulator.
  2. Hardware/Firmware architecture fix: Implement a dual-range circuit using a second reference resistor ($50\text{ k}\Omega\text{ or }100\text{ k}\Omega$) switched via an open-drain GPIO or analog switch for resistances above $8.5\text{ k}\Omega$ ($< 10\ \mu\text{S/cm}$).

---

### Bug 4: Inexact Linear Slope for PT1000 RTD Conversion

- **Severity:** **MEDIUM** (Temperature Measurement Drift)
- **File:** [Core/Src/meter_core.c](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L108-L110)
- **Lines:** 108–110
- **Root Cause:** Code calculates temperature as:
  $$T = \frac{R - 1000.0}{3.85}$$
  Omitting the quadratic term ($B = -5.775 \times 10^{-7}$) produces $+0.29^\circ\text{C}$ error at $25^\circ\text{C}$ and $+0.38^\circ\text{C}$ error at $50^\circ\text{C}$.
- **Remediation Plan:** Implement the exact Callendar-Van Dusen inverse formula:
  $$T = \frac{-A + \sqrt{A^2 - 4B(1 - R/1000.0)}}{2B}$$
  Where $A = 3.9083 \times 10^{-3}$, $B = -5.775 \times 10^{-7}$.

---

### Bug 5: Cell Constant ($K$) Menu Restricted to 3 Decimals

- **Severity:** **MEDIUM** (Systematic Scaling Error)
- **File:** [Core/Src/meter_ui.c](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_ui.c#L1590)
- **Lines:** 1590
- **Root Cause:** Menu editor formats $K$ using `"%06.3f"`, which rounds $K = 0.0852$ down to $0.0850$ (an uncompensated $-0.235\%$ error across all readings).
- **Remediation Plan:** Change format specifier to `"%07.4f"` with 4 decimal digits, allowing direct entry of $0.0852$.

---

### Bug 6: Factory Default Probe Type is NTC10K

- **Severity:** **MEDIUM** (Incorrect Out-of-Box Operation)
- **File:** [Core/Src/meter_core.c](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L33)
- **Lines:** 33
- **Root Cause:** `s->temp_probe_type = TEMP_PROBE_NTC10K`. With the attached PT1000 probe ($1097\ \Omega$ at $25^\circ\text{C}$), the Steinhart-Hart NTC routine calculates $85^\circ\text{C}$, causing severe over-compensation.
- **Remediation Plan:** Set factory default to `s->temp_probe_type = TEMP_PROBE_PT1000;`.

---

### Bug 7: Over-Range Trigger Threshold Set to Meter Full-Scale Instead of Probe Range

- **Severity:** **LOW** (Metrology Warning Threshold)
- **File:** [Core/Src/meter_core.c](file:///c:/Users/srava/Documents/smt32/Core/Src/meter_core.c#L362-L365)
- **Lines:** 362–365
- **Root Cause:** `out_of_range` only asserts when $EC > 20000.0\ \mu\text{S/cm}$ ($20\text{ mS/cm}$). The physical cell limit is $200.0\ \mu\text{S/cm}$.
- **Remediation Plan:** Evaluate probe cell constant: when $K \le 0.1\text{ cm}^{-1}$, assert `out_of_range` if $EC > 200.0\ \mu\text{S/cm}$.

---

## 4. Hardware Verification Protocol

Conduct these tests on the physical bench setup prior to field commissioning:

```
                  ┌────────────────────────────────────────┐
                  │          STM32G030 CONTROLLER          │
                  │   PA6 (EXC_A)   PA7 (EXC_B)  PA0 (SENSE│
                  └───┬─────────────┬────────────┬─────────┘
                      │             │            │
                      ▼             ▼            ▼
             ┌──────────────────────────────────────────────┐
             │         PRECISION DECADE RESISTOR BOX        │
             │   426.0 Ω  /  852.0 Ω  /  1014.3 Ω  /  170kΩ │
             └──────────────────────────────────────────────┘
```

1. **Conductivity Decade Box Verification:**
   - $426.0\ \Omega \implies 200.0\ \mu\text{S/cm}$
   - $852.0\ \Omega \implies 100.0\ \mu\text{S/cm}$
   - $1014.3\ \Omega \implies 84.0\ \mu\text{S/cm}$
   - $8520.0\ \Omega \implies 10.0\ \mu\text{S/cm}$
   - $170400.0\ \Omega \implies 0.5\ \mu\text{S/cm}$
2. **PT1000 RTD Simulation:**
   - $1000.0\ \Omega \implies 0.0^\circ\text{C}$
   - $1097.4\ \Omega \implies 25.0^\circ\text{C}$
   - $1194.0\ \Omega \implies 50.0^\circ\text{C}$
   - $1385.1\ \Omega \implies 100.0^\circ\text{C}$
3. **Certified Standard Solution Wet Test:**
   - Test with certified $84.0\ \mu\text{S/cm}$ standard solution at $25.0^\circ\text{C}$.
   - _Caution:_ Do not immerse this $K=0.1$ probe into standard $1413\ \mu\text{S/cm}$ solution; $R_{cell} \approx 60\ \Omega$ will cause severe non-linear polarization.
4. **Three-Way Coherence Check:**
   - Verify that the physical **ST7565 LCD**, the serial port monitor **`[FEED]` broadcast**, and the **Modbus RTU master response** (holding registers 0, 2, 4, 12, 14) are identical.

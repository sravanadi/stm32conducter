@echo off
chcp 65001 >nul
title Mirrant TDS 4002 - Real-Time Serial Monitor (STM32G030K8T6)
cls
echo ====================================================================
echo        MIRRANT TDS 4002 SERIAL MONITOR & REAL-TIME DATA FEED
echo ====================================================================
echo  Hardware Ports:
echo    MCU TX : PB6 (USART1_TX, MCU Pin 30)
echo    MCU RX : PB7 (USART1_RX, MCU Pin 31)
echo    RS-485 : PB5 (THVD1450 RE/DE Direction, MCU Pin 29)
echo    Ground : J3 SWD Header Pin 4 (GND) or Pin 1 (3.3V)
echo    RS-485 : Terminal Block A (D+) and B (D-)
echo ====================================================================
echo.

set PORT_ARG=
set BAUD_ARG=

if not "%~1"=="" set PORT_ARG=-Port "%~1"
if not "%~2"=="" set BAUD_ARG=-Baud %~2

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0serial_monitor.ps1" %PORT_ARG% %BAUD_ARG%

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Serial monitor stopped.
    pause
)

@echo off
chcp 65001 >nul
title Mirrant TDS 4002 - Live SWD Telemetry Monitor (J3 Header)
cls
echo ====================================================================
echo      MIRRANT TDS 4002 - LIVE SWD TELEMETRY MONITOR (VIA J3)
echo ====================================================================
echo  Connection : ST-LINK on J3 Header (SWDIO, SWCLK, GND, 3.3V)
echo  No extra UART / RS-485 cables required!
echo ====================================================================
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0swd_monitor.ps1"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo SWD Monitor stopped.
    pause
)

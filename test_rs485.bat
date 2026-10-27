@echo off
chcp 65001 >nul
title Mirrant TDS 4002 - RS-485 Test & Transfer Tool
cls
echo ====================================================================
echo        MIRRANT TDS 4002 - RS-485 DATA TRANSFER ^& TEST TOOL
echo ====================================================================
echo.
echo  Available Options:
echo    [1] Run Full Diagnostics on COM6 (Tests ASCII, Modbus, and Stream)
echo    [2] Interactive Send/Receive Console (Type commands and see replies)
echo    [3] Run Diagnostics on a different COM port
echo.
set /p choice="Select an option (1-3, default 1): "

if "%choice%"=="2" goto interactive
if "%choice%"=="3" goto custom_port

python test_rs485.py -p COM6
goto end

:interactive
python test_rs485.py -p COM6 -i
goto end

:custom_port
set /p custom_port="Enter COM port (e.g. COM3): "
python test_rs485.py -p %custom_port%
goto end

:end
echo.
pause

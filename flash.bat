@echo off
echo ========================================================
echo Flashing Sensor Display Firmware to STM32G030K8T6
echo ========================================================
"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD freq=1800 mode=NORMAL -w "%~dp0build\display.elf" -v -rst
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [Retrying with Under-Reset mode...]
    "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD freq=1800 mode=UR -w "%~dp0build\display.elf" -v -rst
)

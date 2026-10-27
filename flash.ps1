$cli = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
$elf = Join-Path $PSScriptRoot "build\display.elf"

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host " Flashing Sensor Display Firmware to STM32G030K8T6" -ForegroundColor Cyan
Write-Host " Binary: $elf" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

& $cli -c port=SWD freq=1800 mode=NORMAL -w $elf -v -rst
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[Retrying with Under-Reset mode (UR)]..." -ForegroundColor Yellow
    & $cli -c port=SWD freq=1800 mode=UR -w $elf -v -rst
}

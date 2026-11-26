# Build Script for Sensor Display (STM32G030K8T6)
$ErrorActionPreference = "Stop"

$toolchainDir = "C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin"
if (-not (Test-Path $toolchainDir)) {
    # Fallback search if path varies
    $found = Get-ChildItem -Path "C:\ST" -Recurse -Filter "arm-none-eabi-gcc.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        $toolchainDir = $found.DirectoryName
    } else {
        Write-Error "ARM GCC toolchain directory not found in C:\ST!"
        exit 1
    }
}

$gcc     = Join-Path $toolchainDir "arm-none-eabi-gcc.exe"
$objcopy = Join-Path $toolchainDir "arm-none-eabi-objcopy.exe"
$size    = Join-Path $toolchainDir "arm-none-eabi-size.exe"

$buildDir = Join-Path $PSScriptRoot "build"
if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

$includes = @(
    "-ICore/Inc",
    "-ICore/serial_port_mointor",
    "-IDrivers/STM32G0xx_HAL_Driver/Inc",
    "-IDrivers/CMSIS/Device/ST/STM32G0xx/Include",
    "-IDrivers/CMSIS/Include"
)

$defines = @(
    "-DSTM32G030xx",
    "-DUSE_HAL_DRIVER"
)

$cflags = @(
    "-mcpu=cortex-m0plus",
    "-mthumb",
    "-O2",
    "-Wall",
    "-Wextra",
    "-fdata-sections",
    "-ffunction-sections"
) + $defines + $includes

$ldflags = @(
    "-mcpu=cortex-m0plus",
    "-mthumb",
    "-TSTM32G030K8TX_FLASH.ld",
    "-Wl,-Map=$buildDir\display.map",
    "-Wl,--gc-sections",
    "-static",
    "--specs=nano.specs",
    "-specs=nosys.specs",
    "-lm"
)

# Source files
$sources = @(
    "Core/Startup/startup_stm32g030k8tx.s",
    "Core/Src/main.c",
    "Core/Src/lcd_st7565.c",
    "Core/Src/buttons.c",
    "Core/Src/syscalls.c",
    "Core/Src/stm32g0xx_it.c",
    "Core/Src/system_stm32g0xx.c",
    "Core/Src/meter_core.c",
    "Core/Src/meter_flash.c",
    "Core/Src/meter_afe.c",
    "Core/Src/meter_ui.c",
    "Core/Src/meter_comm.c",
    "Core/Src/turbidity_sensor.c",
    "Core/serial_port_mointor/serial_port_mointor.c",
    "Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal.c",
    "Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_cortex.c",
    "Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_gpio.c",
    "Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_pwr.c",
    "Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_pwr_ex.c",
    "Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_rcc.c",
    "Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_rcc_ex.c",
    "Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_spi.c"
)

$objects = @()

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host " Compiling Sensor Display Firmware (STM32G030K8T6)" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

foreach ($src in $sources) {
    $srcName = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $objFile = Join-Path $buildDir "$srcName.o"
    $objects += $objFile

    Write-Host "  CC  $src" -ForegroundColor Gray
    if ($src.EndsWith(".s")) {
        & $gcc -mcpu=cortex-m0plus -mthumb -c $src -o $objFile
    } else {
        & $gcc @cflags -c $src -o $objFile
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Compilation failed for $src"
        exit 1
    }
}

$elfFile = Join-Path $buildDir "display.elf"
$hexFile = Join-Path $buildDir "display.hex"
$binFile = Join-Path $buildDir "display.bin"

Write-Host "`nLinking: $elfFile..." -ForegroundColor Yellow
& $gcc @ldflags @objects -o $elfFile
if ($LASTEXITCODE -ne 0) {
    Write-Error "Linking failed!"
    exit 1
}

Write-Host "Generating HEX and BIN..." -ForegroundColor Gray
& $objcopy -O ihex $elfFile $hexFile
& $objcopy -O binary $elfFile $binFile

Write-Host "`nMemory Usage:" -ForegroundColor Green
& $size $elfFile

Write-Host "`n[BUILD SUCCESSFUL] Firmware ready: $elfFile`n" -ForegroundColor Green

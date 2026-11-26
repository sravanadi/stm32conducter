/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body for Sensor Display Controller
  *                   Target MCU: STM32G030K8T6 (64KB Flash, 8KB RAM, Cortex-M0+)
  *                   Display   : NHD-C12864LZ-FSW-FBW-3V3 (ST7565R SPI)
  ******************************************************************************
  */

#include "main.h"
#include "meter_core.h"
#include "meter_flash.h"
#include "meter_afe.h"
#include "meter_ui.h"
#include "meter_comm.h"
#include "turbidity_sensor.h"
#include "serial_port_mointor.h"

/* Peripheral Handles */
SPI_HandleTypeDef hspi1;

/* Private function prototypes */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);

static settings_t      s_settings;
static meter_reading_t s_reading;

const meter_reading_t *meter_data(void)
{
    return &s_reading;
}

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* Configure the system clock: 16 MHz internal HSI */
    SystemClock_Config();

    /* Initialize all configured GPIO pins (Relays OFF, RS485 RX mode, Buttons, LCD pins) */
    MX_GPIO_Init();

    /* Initialize SPI1 peripheral (PA1 = SCK, PA2 = MOSI) */
    MX_SPI1_Init();

    /* Load persistent configuration from Flash Page 31 (0x0800F800) */
    meter_flash_load(&s_settings);

    /* Initialize Display */
    lcd_init();
    if (s_settings.contrast > 0 && s_settings.contrast < 64)
    {
        lcd_set_contrast(s_settings.contrast);
    }

    /* Show WELCOME splash screen for 2.0 seconds matching specification */
    lcd_clear();
    lcd_print_font_centered(LCD_FONT_8X16, 3, "WELCOME");
    HAL_Delay(2000);
    lcd_clear();

    /* Initialize tactile push buttons (PB1=SW4, PB2=SW3, PB3=SW2, PB4=SW1) */
    buttons_init();

    /* Initialize Analog Front End (ADC1 on PA0 & PA5, Excitation on PA6 & PA7) */
    meter_afe_init();

    /* Initialize RS-485 Modbus RTU interface on USART1 */
    meter_comm_init(&s_settings);

    /* Initialize Digital Turbidity Sensor Master driver */
    turbidity_init();

    /* Initialize Mirrant User Interface */
    meter_ui_init(&s_settings, &s_reading);

    uint32_t last_measure_time = 0;

    /*
     * Main application loop:
     * Non-blocking real-time measurement, Modbus RTU communication, and UI navigation
     */
    while (1)
    {
        uint32_t now = HAL_GetTick();

        /* 1. Periodic measurement & calculations (every 100 ms) */
        if ((now - last_measure_time) >= 100)
        {
            last_measure_time = now;

            float cell_ohms = 0.0f;
            float temp_ohms = 0.0f;
            afe_read(&cell_ohms, &temp_ohms);

            /* Perform live conductivity, resistivity, TDS, salinity calculations */
            meter_compute(cell_ohms, temp_ohms, &s_settings, &s_reading);

            /* Step relay control state machine with hysteresis and wash cycles */
            relay_step(&s_reading, &s_settings, now);

            /* Update physical relay GPIOs (PA3 = Relay 1, PA4 = Relay 2) */
            int8_t r1_ovr = meter_ui_relay_test_state(0);
            if (r1_ovr < 0) r1_ovr = serial_monitor_relay_override(0);
            uint8_t r1_val = (r1_ovr >= 0) ? (uint8_t)r1_ovr : s_reading.relay1_state;
            s_reading.relay1_state = r1_val;
            if (r1_val)
            {
                BOARD_RELAY1_ON();
            }
            else
            {
                BOARD_RELAY1_OFF();
            }

            int8_t r2_ovr = meter_ui_relay_test_state(1);
            if (r2_ovr < 0) r2_ovr = serial_monitor_relay_override(1);
            uint8_t r2_val = (r2_ovr >= 0) ? (uint8_t)r2_ovr : s_reading.relay2_state;
            s_reading.relay2_state = r2_val;
            if (r2_val)
            {
                BOARD_RELAY2_ON();
            }
            else
            {
                BOARD_RELAY2_OFF();
            }
        }

        /* 2. Poll Turbidity Sensor & process RS-485 Modbus RTU communications */
        turbidity_task(now);
        meter_comm_task(&s_reading, &s_settings);

        /* 3. Process button events and refresh LCD interface */
        meter_ui_task();

        HAL_Delay(1);
    }
}

/**
  * @brief System Clock Configuration
  *        Configures HSI 16 MHz oscillator as SYSCLK source.
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Configure the main internal regulator output voltage */
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    /** Initializes the RCC Oscillators according to the specified parameters
    * in the RCC_OscInitTypeDef structure.
    */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief SPI1 Initialization Function
  *        Mode: Master, Transmit-only / Full-Duplex, 8-bit, Mode 0 (CPOL=0, CPHA=0)
  *        Baud Rate: 16 MHz / 4 = 4.0 MHz (well within ST7565R 20 MHz limit)
  * @retval None
  */
static void MX_SPI1_Init(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
    hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;

    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief SPI1 MSP Initialization
  *        Enables SPI1 clock and configures PA1 (SCK) and PA2 (MOSI)
  * @param hspi: SPI handle pointer
  * @retval None
  */
void HAL_SPI_MspInit(SPI_HandleTypeDef* hspi)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (hspi->Instance == SPI1)
    {
        /* Peripheral clock enable */
        __HAL_RCC_SPI1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        /**SPI1 GPIO Configuration
        PA1     ------> SPI1_SCK (AF0)
        PA2     ------> SPI1_MOSI (AF0)
        */
        GPIO_InitStruct.Pin = LCD_SCK_PIN | LCD_MOSI_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF0_SPI1;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

/**
  * @brief GPIO Initialization Function
  *        Configures all board GPIOs according to schematic sens_display_02may25_1947.pdf.
  * @retval None
  */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* ====================================================================== */
    /* 1. RELAY OUTPUTS (PA3 = RELAY_1, PA4 = RELAY_2)                        */
    /*    MUST be initialized LOW to keep relays OFF at boot!                 */
    /* ====================================================================== */
    BOARD_RELAY1_OFF();
    BOARD_RELAY2_OFF();

    GPIO_InitStruct.Pin = RELAY_1_PIN | RELAY_2_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* ====================================================================== */
    /* 2. LCD CONTROL OUTPUTS                                                 */
    /*    PA8  = LCD_CS   (Active LOW, default HIGH)                          */
    /*    PA9  = LCD_RES  (Active LOW, default HIGH)                          */
    /*    PA10 = LCD_A0   (LOW=CMD, HIGH=DATA, default LOW)                   */
    /*    PA11 = LCD_RW   (In serial mode, driven HIGH)                       */
    /*    PA12 = LCD_RD   (In serial mode, driven HIGH)                       */
    /* ====================================================================== */
    HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_RES_PORT, LCD_RES_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_A0_PORT, LCD_A0_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_RW_PORT, LCD_RW_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_RD_PORT, LCD_RD_PIN, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = LCD_CS_PIN | LCD_RES_PIN | LCD_A0_PIN | LCD_RW_PIN | LCD_RD_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* ====================================================================== */
    /* 3. LCD BACKLIGHT ENABLE (PC6 = LCD_LED_EN)                             */
    /*    Controls AD8591 shutdown pin (Active HIGH). Default LOW at power-up */
    /* ====================================================================== */
    HAL_GPIO_WritePin(LCD_LED_EN_PORT, LCD_LED_EN_PIN, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = LCD_LED_EN_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LCD_LED_EN_PORT, &GPIO_InitStruct);

    /* ====================================================================== */
    /* 4. PUSH BUTTON INPUTS (PB1=SW4, PB2=SW3, PB3=SW2, PB4=SW1)             */
    /*    External 10k pull-ups on PCB; internal pull-up enabled for safety.  */
    /* ====================================================================== */
    GPIO_InitStruct.Pin = SW1_PIN | SW2_PIN | SW3_PIN | SW4_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* ====================================================================== */
    /* 5. RS-485 CONTROL (PB5 = 485_REDE)                                     */
    /*    LOW = Receive mode (driver disabled), HIGH = Transmit mode          */
    /* ====================================================================== */
    BOARD_RS485_RX_MODE();

    GPIO_InitStruct.Pin = RS485_REDE_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RS485_REDE_PORT, &GPIO_InitStruct);

    /* ====================================================================== */
    /* 6. INA226 ALERT INPUT (PB0 = ADC_ALERT)                                */
    /* ====================================================================== */
    GPIO_InitStruct.Pin = INA226_ALERT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(INA226_ALERT_PORT, &GPIO_InitStruct);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT
void asserst_failed(uint8_t *file, uint32_t line)
{
}
#endif

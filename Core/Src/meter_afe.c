/**
 * @file    meter_afe.c
 * @brief   Analog Front End (AFE) driver for conductivity cell and temperature probe.
 */

#include "meter_afe.h"
#include "stm32g030xx.h"

/* ASSUMPTION: Reference series divider resistor on PA6 is 1000 Ohms (0.1%) */
#define AFE_R_REF           1000.0f

/* ASSUMPTION: Temperature pull-up resistor on PA5 is 10000 Ohms (0.1%) to 3.3V */
#define AFE_R_PULLUP_TEMP   10000.0f

static void delay_us(uint32_t us)
{
    /* At 16 MHz, 1 us is ~16 cycles; loop overhead is ~4 cycles */
    volatile uint32_t count = us * 3;
    while (count--) { __NOP(); }
}

static uint16_t adc_sample_channel(uint32_t channel)
{
    /* Select channel */
    ADC1->CHSELR = (1UL << channel);
    while (!(ADC1->ISR & ADC_ISR_CCRDY)) { /* Wait channel ready */ }

    /* Clear EOC */
    ADC1->ISR |= ADC_ISR_EOC;

    /* Start conversion */
    ADC1->CR |= ADC_CR_ADSTART;
    while (!(ADC1->ISR & ADC_ISR_EOC)) { /* Wait end of conversion */ }

    return (uint16_t)ADC1->DR;
}

void meter_afe_init(void)
{
    /* 1. Enable GPIOA and ADC1 clocks */
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    RCC->APBENR2 |= RCC_APBENR2_ADCEN;

    /* 2. Configure PA0 (Pin 7) & PA5 (Pin 12) as Analog Inputs (MODER = 0x3) with Pull-Down (PUPDR = 0x2) */
    GPIOA->MODER |= (3UL << (0 * 2)) | (3UL << (5 * 2));
    GPIOA->PUPDR &= ~((3UL << (0 * 2)) | (3UL << (5 * 2)));
    GPIOA->PUPDR |= (2UL << (0 * 2)) | (2UL << (5 * 2)); /* Enable internal pull-downs */

    /* 3. Configure PA6 (Pin 13) & PA7 (Pin 14) as Push-Pull Outputs (MODER = 0x1) */
    GPIOA->MODER &= ~((3UL << (6 * 2)) | (3UL << (7 * 2)));
    GPIOA->MODER |= (1UL << (6 * 2)) | (1UL << (7 * 2));
    GPIOA->OTYPER &= ~((1UL << 6) | (1UL << 7));
    GPIOA->OSPEEDR |= (3UL << (6 * 2)) | (3UL << (7 * 2));
    GPIOA->ODR &= ~((1UL << 6) | (1UL << 7)); /* Start LOW */

    /* 4. ADC Voltage Regulator Startup */
    ADC1->CR |= ADC_CR_ADVREGEN;
    delay_us(25); /* Wait for regulator to stabilize (min 20 us) */

    /* 5. Calibrate ADC */
    ADC1->CR |= ADC_CR_ADCAL;
    while (ADC1->CR & ADC_CR_ADCAL) { /* Wait for calibration */ }

    /* 6. Enable ADC */
    ADC1->ISR |= ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    while (!(ADC1->ISR & ADC_ISR_ADRDY)) { /* Wait for ADC ready */ }

    /* Configure sampling time (longer sampling for higher accuracy) */
    ADC1->SMPR = (0x07); /* 160.5 ADC clock cycles */
}

bool meter_afe_read(float *cell_ohms, float *temp_ohms)
{
    if (!cell_ohms || !temp_ohms) return false;

    /* ====================================================================== */
    /* 0. Daughterboard Presence Detection & Floating Noise Suppression      */
    /* ====================================================================== */
    /* Ensure pull-downs active while excitation pins PA6 and PA7 are LOW (0V) */
    GPIOA->ODR &= ~((1UL << 6) | (1UL << 7));
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3UL << (0 * 2)) | (3UL << (5 * 2)))) |
                   ((2UL << (0 * 2)) | (2UL << (5 * 2)));
    delay_us(25);

    uint16_t ch0_idle = adc_sample_channel(0);
    uint16_t ch5_idle = adc_sample_channel(5);

    /* When no daughterboard is plugged into J2, both PA0 & PA5 are open header pins */
    /* held firmly at 0V by the internal pull-downs (ADC counts < 35).               */
    if (ch0_idle < 35 && ch5_idle < 35)
    {
        /* No daughterboard attached: output clean zeros and suppress all floating noise */
        *cell_ohms = 0.0f;
        *temp_ohms = 0.0f;
        return true;
    }

    /* Daughterboard detected: temporarily disable pull-downs for 100% accurate measurement */
    GPIOA->PUPDR &= ~((3UL << (0 * 2)) | (3UL << (5 * 2)));
    delay_us(15);

    /* ====================================================================== */
    /* 1. Bipolar AC Excitation & Symmetrical Dual-Phase Sampling             */
    /* ====================================================================== */
    uint32_t adc_fwd_sum = 0;
    uint32_t adc_rev_sum = 0;
    const uint8_t OVERSAMPLE = 32;

    for (uint8_t i = 0; i < OVERSAMPLE; i++)
    {
        /* Positive Phase: PA6 (EXC_A) = HIGH (3.3V), PA7 (EXC_B) = LOW (0V) */
        GPIOA->BSRR = (1UL << 6) | (1UL << (7 + 16));
        delay_us(80); /* Wait for settling */

        adc_fwd_sum += adc_sample_channel(0); /* Sample PA0 (ADC_IN0) */

        /* Negative Phase (Reverse Polarity): PA6 = LOW, PA7 = HIGH */
        GPIOA->BSRR = (1UL << (6 + 16)) | (1UL << 7);
        delay_us(80);

        adc_rev_sum += adc_sample_channel(0); /* Sample PA0 in reverse phase */

        /* Return to Neutral Ground State */
        GPIOA->BSRR = (1UL << (6 + 16)) | (1UL << (7 + 16));
        delay_us(20);
    }

    float avg_fwd = (float)adc_fwd_sum / (float)OVERSAMPLE;
    float avg_rev = (float)adc_rev_sum / (float)OVERSAMPLE;

    /* Symmetrical AC demodulation:
     * In forward phase: V_sense = V_dd * R_cell / (R_ref + R_cell)
     * In reverse phase: V_sense = V_dd * R_ref / (R_ref + R_cell)
     * Therefore: (4095 - avg_rev) is a direct complementary measurement of R_cell.
     * Averaging both half-cycles cancels out any DC galvanic contact offset!
     */
    float avg_adc_cell = (avg_fwd + (4095.0f - avg_rev)) * 0.5f;

    /* Cell voltage divider: V_cell = V_dd * (R_cell / (R_ref + R_cell)) */
    /* -> R_cell = R_ref * ADC / (4095.0 - ADC) */
    if (avg_adc_cell <= 2.0f)
    {
        /* Shorted electrodes or tied to GND */
        *cell_ohms = 0.1f;
    }
    else if (avg_adc_cell >= 4092.0f)
    {
        /* Open electrodes (disconnected probe in air) */
        *cell_ohms = 20000000.0f;
    }
    else
    {
        *cell_ohms = AFE_R_REF * avg_adc_cell / (4095.0f - avg_adc_cell);
    }

    /* ====================================================================== */
    /* 2. Temperature Probe Sampling (PA5 = ADC_IN5)                          */
    /* ====================================================================== */
    uint32_t adc_temp_sum = 0;
    for (uint8_t i = 0; i < OVERSAMPLE; i++)
    {
        adc_temp_sum += adc_sample_channel(5);
        delay_us(10);
    }

    float avg_adc_temp = (float)adc_temp_sum / (float)OVERSAMPLE;

    /* Re-enable pull-downs during idle to prevent any EMI coupling */
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3UL << (0 * 2)) | (3UL << (5 * 2)))) |
                   ((2UL << (0 * 2)) | (2UL << (5 * 2)));

    /* NTC divider: V_out = V_dd * (R_ntc / (R_pullup + R_ntc)) */
    /* -> R_ntc = R_pullup * ADC / (4095.0 - ADC) */
    if (avg_adc_temp <= 5.0f)
    {
        *temp_ohms = 1.0f; /* Shorted sensor */
    }
    else if (avg_adc_temp >= 4085.0f)
    {
        *temp_ohms = 9999999.0f; /* Open sensor */
    }
    else
    {
        *temp_ohms = AFE_R_PULLUP_TEMP * avg_adc_temp / (4095.0f - avg_adc_temp);
    }

    return true;
}

bool afe_read(float *cell_ohms, float *temp_ohms)
{
    return meter_afe_read(cell_ohms, temp_ohms);
}


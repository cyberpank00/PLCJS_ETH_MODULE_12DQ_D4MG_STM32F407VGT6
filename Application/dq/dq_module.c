/**
  ******************************************************************************
  * @file    dq_module.c
  * @brief   12-channel discrete output driver.
  *
  * Each channel maps to a single push-pull GPIO. The published logical state
  * is cached in a 12-bit mask so reads never touch the hardware. Writes update
  * both the cached mask and the physical pin. Polarity is centralised in
  * dq_write_raw() via DQ_ACTIVE_HIGH.
  ******************************************************************************
  */

#include "dq_module.h"

#include "main.h"
#include "stm32f4xx_hal.h"

/* ---------------------------------------------------------------------------
 * Pin map (taken from main.h / gpio.c). Index 0 corresponds to DQ1 in the
 * Modbus map: coil 0 == channel index 0 == DQ1 on the silkscreen.
 * The .ioc labels are DQ0..DQ11 (with DQ0 on PB3 and DQ1 on PD7), and the
 * silkscreen labels are DQ1..DQ12, so we publish a stable mapping here.
 * ------------------------------------------------------------------------- */
typedef struct {
    GPIO_TypeDef* port;
    uint16_t      pin;
} dq_pin_t;

static const dq_pin_t s_pins[DQ_MODULE_CHANNEL_COUNT] = {
    /* idx -> Modbus coil */
    { DQ0_GPIO_Port,  DQ0_Pin  },   /*  0 -> DQ1 (PB3)  */
    { DQ1_GPIO_Port,  DQ1_Pin  },   /*  1 -> DQ2 (PD7)  */
    { DQ2_GPIO_Port,  DQ2_Pin  },   /*  2 -> DQ3 (PD6)  */
    { DQ3_GPIO_Port,  DQ3_Pin  },   /*  3 -> DQ4 (PD5)  */
    { DQ4_GPIO_Port,  DQ4_Pin  },   /*  4 -> DQ5 (PD4)  */
    { DQ5_GPIO_Port,  DQ5_Pin  },   /*  5 -> DQ6 (PD3)  */
    { DQ6_GPIO_Port,  DQ6_Pin  },   /*  6 -> DQ7 (PD2)  */
    { DQ7_GPIO_Port,  DQ7_Pin  },   /*  7 -> DQ8 (PD1)  */
    { DQ8_GPIO_Port,  DQ8_Pin  },   /*  8 -> DQ9 (PD0)  */
    { DQ9_GPIO_Port,  DQ9_Pin  },   /*  9 -> DQ10 (PC12) */
    { DQ10_GPIO_Port, DQ10_Pin },   /* 10 -> DQ11 (PC11) */
    { DQ11_GPIO_Port, DQ11_Pin },   /* 11 -> DQ12 (PC10) */
};

/* ---------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */
static uint16_t s_mask;     /* cached logical output state */

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */
static inline void dq_write_raw(uint8_t idx, uint8_t on)
{
#if DQ_ACTIVE_HIGH
    const GPIO_PinState lvl = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
#else
    const GPIO_PinState lvl = on ? GPIO_PIN_RESET : GPIO_PIN_SET;
#endif
    HAL_GPIO_WritePin(s_pins[idx].port, s_pins[idx].pin, lvl);
}

static void dq_configure_outputs(void)
{
    /* Configure all twelve DQ pins as push-pull outputs. We group by port to
     * share the GPIO_InitTypeDef. The CubeMX-generated gpio.c already sets
     * these pins up; reapplying here makes the driver self-contained and
     * independent of any later reconfiguration. */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    /* PB: DQ0 (= DQ1 logical, PB3) */
    gpio.Pin = DQ0_Pin;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PC: DQ9, DQ10, DQ11 */
    gpio.Pin = DQ9_Pin | DQ10_Pin | DQ11_Pin;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PD: DQ1, DQ2, DQ3, DQ4, DQ5, DQ6, DQ7, DQ8 */
    gpio.Pin = DQ1_Pin | DQ2_Pin | DQ3_Pin | DQ4_Pin |
               DQ5_Pin | DQ6_Pin | DQ7_Pin | DQ8_Pin;
    HAL_GPIO_Init(GPIOD, &gpio);
}

/* ---------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */
void dq_module_init(uint16_t initial_mask)
{
    dq_configure_outputs();
    dq_module_set_mask(initial_mask);
}

void dq_module_set_output(uint8_t idx, bool on)
{
    if (idx >= DQ_MODULE_CHANNEL_COUNT) {
        return;
    }
    if (on) {
        s_mask |= (uint16_t)(1u << idx);
    } else {
        s_mask &= (uint16_t)~(1u << idx);
    }
    dq_write_raw(idx, on ? 1u : 0u);
}

bool dq_module_get_output(uint8_t idx)
{
    if (idx >= DQ_MODULE_CHANNEL_COUNT) {
        return false;
    }
    return (s_mask & (uint16_t)(1u << idx)) != 0u;
}

void dq_module_set_mask(uint16_t mask)
{
    mask &= DQ_MASK_ALL;
    s_mask = mask;
    for (uint8_t i = 0; i < DQ_MODULE_CHANNEL_COUNT; i++) {
        dq_write_raw(i, (mask & (uint16_t)(1u << i)) ? 1u : 0u);
    }
}

uint16_t dq_module_get_mask(void)
{
    return s_mask;
}

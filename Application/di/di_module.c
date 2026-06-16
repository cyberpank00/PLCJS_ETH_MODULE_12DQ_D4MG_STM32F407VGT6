/**
  ******************************************************************************
  * @file    di_module.c
  * @brief   12-channel discrete input driver with stability-counter filter.
  *
  * Filtering algorithm (per channel):
  *   - Each tick (1 ms) we read the raw level.
  *   - When the raw level matches the currently published "stable" state,
  *     the channel's match counter is reset.
  *   - When the raw level differs, the counter is incremented; once it
  *     reaches `filter_ms` consecutive samples, the stable state is flipped.
  *
  * That gives a deterministic, configurable debounce time independent of the
  * input pattern, with O(1) memory per channel.
  ******************************************************************************
  */

#include "di_module.h"

#include "main.h"
#include "stm32f4xx_hal.h"

/* ---------------------------------------------------------------------------
 * Pin map (taken from main.h / gpio.c). Index 0 corresponds to DI1 in the
 * Modbus map: register 0 == channel index 0 == DI1 on the silkscreen.
 * The .ioc labels are DI0..DI11 (with DI0 on PB3 and DI1 on PD7), and the
 * silkscreen labels are DI1..DI12, so we publish a stable mapping here.
 * ------------------------------------------------------------------------- */
typedef struct {
    GPIO_TypeDef* port;
    uint16_t      pin;
} di_pin_t;

static const di_pin_t s_pins[DI_MODULE_CHANNEL_COUNT] = {
    /* idx -> Modbus register */
    { DI0_GPIO_Port,  DI0_Pin  },   /*  0 -> DI1 (PB3)  */
    { DI1_GPIO_Port,  DI1_Pin  },   /*  1 -> DI2 (PD7)  */
    { DI2_GPIO_Port,  DI2_Pin  },   /*  2 -> DI3 (PD6)  */
    { DI3_GPIO_Port,  DI3_Pin  },   /*  3 -> DI4 (PD5)  */
    { DI4_GPIO_Port,  DI4_Pin  },   /*  4 -> DI5 (PD4)  */
    { DI5_GPIO_Port,  DI5_Pin  },   /*  5 -> DI6 (PD3)  */
    { DI6_GPIO_Port,  DI6_Pin  },   /*  6 -> DI7 (PD2)  */
    { DI7_GPIO_Port,  DI7_Pin  },   /*  7 -> DI8 (PD1)  */
    { DI8_GPIO_Port,  DI8_Pin  },   /*  8 -> DI9 (PD0)  */
    { DI9_GPIO_Port,  DI9_Pin  },   /*  9 -> DI10 (PC12) */
    { DI10_GPIO_Port, DI10_Pin },   /* 10 -> DI11 (PC11) */
    { DI11_GPIO_Port, DI11_Pin },   /* 11 -> DI12 (PC10) */
};

/* ---------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */
static uint16_t s_filter_ms = 50u;
static uint16_t s_match_cnt[DI_MODULE_CHANNEL_COUNT];
static uint8_t  s_stable[DI_MODULE_CHANNEL_COUNT];   /* 0/1 logical level */
static uint16_t s_mask;                              /* cached output     */

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */
static inline uint8_t di_read_raw(uint8_t idx)
{
    /* Active-low (pin shorted to GND when input is asserted). */
    return (HAL_GPIO_ReadPin(s_pins[idx].port, s_pins[idx].pin) == GPIO_PIN_RESET) ? 1u : 0u;
}

static void di_apply_pullups(void)
{
    /* Override the CubeMX-generated NOPULL with PULLUP without otherwise
     * disturbing the pin configuration. We do this per port to share the
     * GPIO_InitTypeDef. */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    /* PB: DI0 (= DI1 logical, PB3) */
    gpio.Pin = DI0_Pin;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PC: DI9, DI10, DI11 */
    gpio.Pin = DI9_Pin | DI10_Pin | DI11_Pin;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PD: DI1, DI2, DI3, DI4, DI5, DI6, DI7, DI8 */
    gpio.Pin = DI1_Pin | DI2_Pin | DI3_Pin | DI4_Pin |
               DI5_Pin | DI6_Pin | DI7_Pin | DI8_Pin;
    HAL_GPIO_Init(GPIOD, &gpio);
}

/* ---------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */
void di_module_init(uint16_t filter_ms)
{
    di_apply_pullups();

    di_module_set_filter_ms(filter_ms);

    /* Seed the stable state with the current raw level so that we don't emit
     * a spurious edge during startup. */
    s_mask = 0u;
    for (uint8_t i = 0; i < DI_MODULE_CHANNEL_COUNT; i++) {
        const uint8_t raw = di_read_raw(i);
        s_stable[i]    = raw;
        s_match_cnt[i] = 0u;
        if (raw) {
            s_mask |= (uint16_t)(1u << i);
        }
    }
}

void di_module_set_filter_ms(uint16_t filter_ms)
{
    if (filter_ms < DI_FILTER_MS_MIN) {
        filter_ms = DI_FILTER_MS_MIN;
    } else if (filter_ms > DI_FILTER_MS_MAX) {
        filter_ms = DI_FILTER_MS_MAX;
    }
    s_filter_ms = filter_ms;
}

uint16_t di_module_get_filter_ms(void)
{
    return s_filter_ms;
}

bool di_module_get_input(uint8_t idx)
{
    if (idx >= DI_MODULE_CHANNEL_COUNT) {
        return false;
    }
    return s_stable[idx] != 0u;
}

uint16_t di_module_get_mask(void)
{
    return s_mask;
}

void di_module_tick(void)
{
    uint16_t mask = 0u;
    for (uint8_t i = 0; i < DI_MODULE_CHANNEL_COUNT; i++) {
        const uint8_t raw = di_read_raw(i);
        if (raw == s_stable[i]) {
            s_match_cnt[i] = 0u;
        } else {
            s_match_cnt[i]++;
            if (s_match_cnt[i] >= s_filter_ms) {
                s_stable[i]    = raw;
                s_match_cnt[i] = 0u;
            }
        }
        if (s_stable[i]) {
            mask |= (uint16_t)(1u << i);
        }
    }
    s_mask = mask;
}

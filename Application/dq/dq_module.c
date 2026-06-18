/**
  ******************************************************************************
  * @file    dq_module.c
  * @brief   12-channel discrete output driver.
  *
  * The module keeps two 12-bit masks:
  *   - s_cmd_mask : the last state commanded over Modbus.
  *   - s_phys_mask: the state actually driven onto the pins.
  * They differ only while a channel is latched into its communication-loss
  * state. A new command for the channel clears the latch and re-couples the
  * physical output to the commanded value.
  ******************************************************************************
  */

#include "dq_module.h"

#include "main.h"
#include "stm32f4xx_hal.h"

/* ---------------------------------------------------------------------------
 * Pin map (taken from main.h / gpio.c). Index 0 corresponds to DQ1 in the
 * Modbus map: register index 0 == channel index 0 == DQ1 on the silkscreen.
 * The .ioc labels are DQ0..DQ11 (with DQ0 on PC11 and DQ1 on PC10), and the
 * silkscreen labels are DQ1..DQ12, so we publish a stable mapping here.
 * ------------------------------------------------------------------------- */
typedef struct {
    GPIO_TypeDef* port;
    uint16_t      pin;
} dq_pin_t;

static const dq_pin_t s_pins[DQ_MODULE_CHANNEL_COUNT] = {
    /* idx -> Modbus register */
    { DQ0_GPIO_Port,  DQ0_Pin  },   /*  0 -> DQ1 (PC11)  */
    { DQ1_GPIO_Port,  DQ1_Pin  },   /*  1 -> DQ2 (PC10)  */
    { DQ2_GPIO_Port,  DQ2_Pin  },   /*  2 -> DQ3 (PA9)   */
    { DQ3_GPIO_Port,  DQ3_Pin  },   /*  3 -> DQ4 (PA8)   */
    { DQ4_GPIO_Port,  DQ4_Pin  },   /*  4 -> DQ5 (PC9)   */
    { DQ5_GPIO_Port,  DQ5_Pin  },   /*  5 -> DQ6 (PC8)   */
    { DQ6_GPIO_Port,  DQ6_Pin  },   /*  6 -> DQ7 (PD10)  */
    { DQ7_GPIO_Port,  DQ7_Pin  },   /*  7 -> DQ8 (PD9)   */
    { DQ8_GPIO_Port,  DQ8_Pin  },   /*  8 -> DQ9 (PD8)   */
    { DQ9_GPIO_Port,  DQ9_Pin  },   /*  9 -> DQ10 (PE14) */
    { DQ10_GPIO_Port, DQ10_Pin },   /* 10 -> DQ11 (PE13) */
    { DQ11_GPIO_Port, DQ11_Pin },   /* 11 -> DQ12 (PE12) */
};

/* ---------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */
static uint16_t         s_cmd_mask;     /* commanded logical state           */
static uint16_t         s_phys_mask;    /* state actually driven on the pins */
static dq_channel_cfg_t s_cfg[DQ_MODULE_CHANNEL_COUNT];
static uint8_t          s_latched[DQ_MODULE_CHANNEL_COUNT]; /* loss applied  */

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */
static inline void dq_write_raw(uint8_t idx, bool on)
{
#if DQ_ACTIVE_HIGH
    const GPIO_PinState level = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
#else
    const GPIO_PinState level = on ? GPIO_PIN_RESET : GPIO_PIN_SET;
#endif
    HAL_GPIO_WritePin(s_pins[idx].port, s_pins[idx].pin, level);
    if (on) {
        s_phys_mask |= (uint16_t)(1u << idx);
    } else {
        s_phys_mask &= (uint16_t)~(1u << idx);
    }
}

static void dq_configure_outputs(void)
{
    /* Drive a known-low level first, then switch the pins to push-pull
     * outputs to avoid a transient on the way out of the CubeMX input config. */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    /* PA: DQ2 (PA9), DQ3 (PA8) */
    HAL_GPIO_WritePin(GPIOA, DQ2_Pin | DQ3_Pin, GPIO_PIN_RESET);
    gpio.Pin = DQ2_Pin | DQ3_Pin;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PC: DQ0 (PC11), DQ1 (PC10), DQ4 (PC9), DQ5 (PC8) */
    HAL_GPIO_WritePin(GPIOC, DQ0_Pin | DQ1_Pin | DQ4_Pin | DQ5_Pin, GPIO_PIN_RESET);
    gpio.Pin = DQ0_Pin | DQ1_Pin | DQ4_Pin | DQ5_Pin;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PD: DQ6 (PD10), DQ7 (PD9), DQ8 (PD8) */
    HAL_GPIO_WritePin(GPIOD, DQ6_Pin | DQ7_Pin | DQ8_Pin, GPIO_PIN_RESET);
    gpio.Pin = DQ6_Pin | DQ7_Pin | DQ8_Pin;
    HAL_GPIO_Init(GPIOD, &gpio);

    /* PE: DQ9 (PE14), DQ10 (PE13), DQ11 (PE12) */
    HAL_GPIO_WritePin(GPIOE, DQ9_Pin | DQ10_Pin | DQ11_Pin, GPIO_PIN_RESET);
    gpio.Pin = DQ9_Pin | DQ10_Pin | DQ11_Pin;
    HAL_GPIO_Init(GPIOE, &gpio);
}

/* ---------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */
void dq_module_init(const dq_channel_cfg_t cfg[DQ_MODULE_CHANNEL_COUNT],
                    uint16_t initial_mask)
{
    dq_configure_outputs();

    s_cmd_mask  = 0u;
    s_phys_mask = 0u;
    for (uint8_t i = 0; i < DQ_MODULE_CHANNEL_COUNT; i++) {
        if (cfg != NULL) {
            s_cfg[i].mode          = (cfg[i].mode > DQ_LOSS_MODE_MAX)
                                         ? DQ_LOSS_MODE_HOLD : cfg[i].mode;
            s_cfg[i].safe_value    = cfg[i].safe_value ? 1u : 0u;
            s_cfg[i].timeout_100ms = cfg[i].timeout_100ms;
        } else {
            s_cfg[i].mode          = DQ_LOSS_MODE_HOLD;
            s_cfg[i].safe_value    = 0u;
            s_cfg[i].timeout_100ms = 0u;
        }
        s_latched[i] = 0u;
    }

    dq_module_set_mask((uint16_t)(initial_mask & DQ_MASK_ALL));
}

void dq_module_set_output(uint8_t idx, bool on)
{
    if (idx >= DQ_MODULE_CHANNEL_COUNT) {
        return;
    }
    if (on) {
        s_cmd_mask |= (uint16_t)(1u << idx);
    } else {
        s_cmd_mask &= (uint16_t)~(1u << idx);
    }
    s_latched[idx] = 0u;
    dq_write_raw(idx, on);
}

bool dq_module_get_output(uint8_t idx)
{
    if (idx >= DQ_MODULE_CHANNEL_COUNT) {
        return false;
    }
    return ((s_phys_mask >> idx) & 1u) != 0u;
}

void dq_module_set_mask(uint16_t mask)
{
    for (uint8_t i = 0; i < DQ_MODULE_CHANNEL_COUNT; i++) {
        dq_module_set_output(i, ((mask >> i) & 1u) != 0u);
    }
}

uint16_t dq_module_get_mask(void)
{
    return s_phys_mask;
}

void dq_module_set_mode(uint8_t idx, uint8_t mode)
{
    if (idx >= DQ_MODULE_CHANNEL_COUNT || mode > DQ_LOSS_MODE_MAX) {
        return;
    }
    s_cfg[idx].mode = mode;
}

uint8_t dq_module_get_mode(uint8_t idx)
{
    return (idx < DQ_MODULE_CHANNEL_COUNT) ? s_cfg[idx].mode : 0u;
}

void dq_module_set_safe_value(uint8_t idx, bool on)
{
    if (idx >= DQ_MODULE_CHANNEL_COUNT) {
        return;
    }
    s_cfg[idx].safe_value = on ? 1u : 0u;
}

bool dq_module_get_safe_value(uint8_t idx)
{
    return (idx < DQ_MODULE_CHANNEL_COUNT) && (s_cfg[idx].safe_value != 0u);
}

void dq_module_set_timeout(uint8_t idx, uint16_t timeout_100ms)
{
    if (idx >= DQ_MODULE_CHANNEL_COUNT) {
        return;
    }
    s_cfg[idx].timeout_100ms = timeout_100ms;
}

uint16_t dq_module_get_timeout(uint8_t idx)
{
    return (idx < DQ_MODULE_CHANNEL_COUNT) ? s_cfg[idx].timeout_100ms : 0u;
}

void dq_module_eval_link_loss(uint32_t ms_since_comm)
{
    for (uint8_t i = 0; i < DQ_MODULE_CHANNEL_COUNT; i++) {
        if (s_cfg[i].mode == DQ_LOSS_MODE_HOLD || s_latched[i]) {
            continue;
        }
        const uint32_t timeout_ms = (uint32_t)s_cfg[i].timeout_100ms * 100u;
        if (ms_since_comm < timeout_ms) {
            continue;
        }
        const bool safe = (s_cfg[i].mode == DQ_LOSS_MODE_SAFE)
                              ? (s_cfg[i].safe_value != 0u) : false;
        dq_write_raw(i, safe);
        s_latched[i] = 1u;
    }
}

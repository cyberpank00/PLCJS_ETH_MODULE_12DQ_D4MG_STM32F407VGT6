/**
  ******************************************************************************
  * @file    dq_module.h
  * @brief   12-channel discrete output driver.
  *
  * Outputs are driven push-pull. Polarity is direct (active-high): a logical
  * "1" drives the corresponding GPIO pin HIGH. Flip DQ_ACTIVE_HIGH to 0 for
  * boards whose output stage inverts the level.
  *
  * Each output has an independent communication-loss behaviour: it can hold
  * the last commanded state, reset to 0 or fall back to a configurable safe
  * value after a per-output timeout. Once a loss action has fired the output
  * is latched in that state until the next explicit command for the channel.
  ******************************************************************************
  */
#ifndef APPLICATION_DQ_MODULE_H
#define APPLICATION_DQ_MODULE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DQ_MODULE_CHANNEL_COUNT     12u
#define DQ_MASK_ALL                 0x0FFFu

/* Output electrical polarity: 1 -> logical "1" drives the pin HIGH. */
#define DQ_ACTIVE_HIGH              1

/* Per-output behaviour on communication loss. */
typedef enum {
    DQ_LOSS_MODE_HOLD = 0,   /* keep the last commanded state              */
    DQ_LOSS_MODE_ZERO = 1,   /* force the output to 0 after the timeout    */
    DQ_LOSS_MODE_SAFE = 2,   /* force the output to its safe value         */
} dq_loss_mode_t;

#define DQ_LOSS_MODE_MAX            2u

/* Per-output configuration (mirrors the persisted settings). */
typedef struct {
    uint8_t  mode;            /* dq_loss_mode_t                            */
    uint8_t  safe_value;      /* 0/1 — used by DQ_LOSS_MODE_SAFE           */
    uint16_t timeout_100ms;   /* comms-loss timeout, units of 100 ms       */
} dq_channel_cfg_t;

/**
 * Initialise the DQ module:
 *  - reconfigures all twelve pins as push-pull outputs (default low)
 *  - loads the per-output configuration
 *  - applies the initial output mask (power-on state)
 */
void dq_module_init(const dq_channel_cfg_t cfg[DQ_MODULE_CHANNEL_COUNT],
                    uint16_t initial_mask);

/* --- Live output control (clears the link-loss latch for the channel) --- */
void dq_module_set_output(uint8_t idx, bool on);
bool dq_module_get_output(uint8_t idx);
void dq_module_set_mask(uint16_t mask);
uint16_t dq_module_get_mask(void);

/* --- Per-output configuration access --- */
void dq_module_set_mode(uint8_t idx, uint8_t mode);
uint8_t dq_module_get_mode(uint8_t idx);
void dq_module_set_safe_value(uint8_t idx, bool on);
bool dq_module_get_safe_value(uint8_t idx);
void dq_module_set_timeout(uint8_t idx, uint16_t timeout_100ms);
uint16_t dq_module_get_timeout(uint8_t idx);

/**
 * Communication-loss evaluator — call periodically from a single task.
 * @param ms_since_comm  milliseconds since the last successful Modbus request.
 *
 * For every channel whose mode is not HOLD, once ms_since_comm reaches the
 * channel timeout the configured loss action is applied and latched. The
 * latch is released by the next dq_module_set_output()/dq_module_set_mask().
 */
void dq_module_eval_link_loss(uint32_t ms_since_comm);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_DQ_MODULE_H */

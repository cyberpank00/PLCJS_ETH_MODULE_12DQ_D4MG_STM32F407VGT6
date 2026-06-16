/**
  ******************************************************************************
  * @file    di_module.h
  * @brief   12-channel discrete input driver with configurable anti-bounce
  *          filter. Inputs are pulled up internally; an active input is
  *          shorted to GND on the board, so logical "1" corresponds to a
  *          GPIO low level.
  ******************************************************************************
  */
#ifndef APPLICATION_DI_MODULE_H
#define APPLICATION_DI_MODULE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DI_MODULE_CHANNEL_COUNT     12u

#define DI_FILTER_MS_MIN            10u
#define DI_FILTER_MS_MAX            1000u

/**
 * Initialise the DI module:
 *  - reconfigures all twelve DI pins as inputs with internal pull-up
 *  - resets debounce state
 *  - applies the given filter time
 */
void di_module_init(uint16_t filter_ms);

/** Set the anti-bounce filter time, ms. The value is clamped to [10, 1000]. */
void di_module_set_filter_ms(uint16_t filter_ms);

/** Get the currently configured filter time in milliseconds. */
uint16_t di_module_get_filter_ms(void);

/** Get the filtered state of a single input (0..11). */
bool di_module_get_input(uint8_t idx);

/** Get a 12-bit mask with one bit per filtered input. */
uint16_t di_module_get_mask(void);

/**
 * Sampling tick — must be called periodically from a single task. The driver
 * was designed for a 1 ms tick (matches the FreeRTOS tick).
 */
void di_module_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_DI_MODULE_H */

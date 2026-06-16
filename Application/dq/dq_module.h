/**
  ******************************************************************************
  * @file    dq_module.h
  * @brief   12-channel discrete output driver. Each channel drives a single
  *          push-pull GPIO. A logical "1" asserts the output; the electrical
  *          level is selected by DQ_ACTIVE_HIGH (active-high by default).
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

/* Mask covering all valid channels (bits 0..11). */
#define DQ_MASK_ALL                 0x0FFFu

/**
 * Output polarity. 1 = a logical "1" drives the MCU pin HIGH (active-high
 * output stage). Set to 0 if the board inverts the signal (e.g. an open-drain
 * sink that is active-low). This is the single place to flip polarity.
 */
#define DQ_ACTIVE_HIGH              1

/**
 * Initialise the DQ module:
 *  - reconfigures all twelve DQ pins as push-pull outputs
 *  - applies the given initial output mask (bit i -> channel i)
 */
void dq_module_init(uint16_t initial_mask);

/** Set a single output (0..11) to the given logical state. */
void dq_module_set_output(uint8_t idx, bool on);

/** Get the logical state of a single output (0..11). */
bool dq_module_get_output(uint8_t idx);

/** Drive all twelve outputs at once from a 12-bit mask (bit i -> channel i). */
void dq_module_set_mask(uint16_t mask);

/** Get a 12-bit mask with one bit per output reflecting its logical state. */
uint16_t dq_module_get_mask(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_DQ_MODULE_H */

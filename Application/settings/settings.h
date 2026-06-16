/**
  ******************************************************************************
  * @file    settings.h
  * @brief   Persistent settings stored in internal Flash with CRC32 protection.
  ******************************************************************************
  */
#ifndef APPLICATION_SETTINGS_H
#define APPLICATION_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Magic and version --------------------------------------------------------- */
#define SETTINGS_MAGIC          0x12D04A57u
#define SETTINGS_VERSION        1u

/* Number of discrete outputs persisted in the settings image. */
#define SETTINGS_DQ_COUNT           12u

/* Defaults ------------------------------------------------------------------ */
/* Discrete-output power-on defaults: all outputs off, HOLD on comms loss,
 * safe value 0, timeout 0 (handled by the memset in reset_to_defaults). */
#define SETTINGS_DEF_DQ_VALUE_MASK  0u
#define SETTINGS_DEF_DQ_SAFE_MASK   0u

#define SETTINGS_DEF_LED_MODE       2u    /* STATE_MACHINE */

#define SETTINGS_DEF_SLAVE_ID       1u
#define SETTINGS_DEF_TCP_PORT       502u

#define SETTINGS_DEF_USE_DHCP       1u

#define SETTINGS_DEF_IP0            192u
#define SETTINGS_DEF_IP1            168u
#define SETTINGS_DEF_IP2            142u
#define SETTINGS_DEF_IP3            147u

#define SETTINGS_DEF_MASK0          255u
#define SETTINGS_DEF_MASK1          255u
#define SETTINGS_DEF_MASK2          255u
#define SETTINGS_DEF_MASK3          0u

#define SETTINGS_DEF_GW0            192u
#define SETTINGS_DEF_GW1            168u
#define SETTINGS_DEF_GW2            142u
#define SETTINGS_DEF_GW3            1u

/* LED mode codes ------------------------------------------------------------ */
typedef enum {
    LED_MODE_ALW_OFF       = 0,
    LED_MODE_ALW_ON        = 1,
    LED_MODE_STATE_MACHINE = 2,
} led_mode_t;

/**
 * Persistent settings structure. Layout is fixed and naturally aligned (the
 * trailing reserved fields keep the size word-aligned). Do not reorder
 * without bumping SETTINGS_VERSION.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved0;

    uint16_t led_mode;              /* led_mode_t                             */

    uint16_t modbus_tcp_port;       /* default 502                            */
    uint8_t  modbus_slave_id;       /* default 1                              */
    uint8_t  use_dhcp;              /* 0 = static, 1 = DHCP                   */

    uint8_t  ip[4];
    uint8_t  netmask[4];
    uint8_t  gateway[4];

    /* Discrete-output configuration (one entry per channel). */
    uint16_t dq_value_mask;                 /* power-on output state          */
    uint16_t dq_safe_mask;                  /* per-output safe value (SAFE)   */
    uint8_t  dq_mode[SETTINGS_DQ_COUNT];    /* dq_loss_mode_t per output      */
    uint16_t dq_timeout[SETTINGS_DQ_COUNT]; /* comms-loss timeout, x100 ms    */

    uint8_t  reserved1[8];

    uint32_t crc32;                 /* CRC32 over all preceding bytes        */
} settings_t;

/* API ----------------------------------------------------------------------- */

/**
 * Initialise the settings subsystem. Loads settings from Flash; if the stored
 * image is invalid the structure is filled with defaults.
 *
 * @return true if the stored image was valid, false if defaults were applied.
 */
bool settings_init(void);

/** Reload defaults into the in-memory settings (does not write to flash). */
void settings_reset_to_defaults(void);

/** Persist the current in-memory settings to internal Flash. */
bool settings_save(void);

/** Get a pointer to the live in-memory settings. */
settings_t* settings_get(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_SETTINGS_H */

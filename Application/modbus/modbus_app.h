/**
  ******************************************************************************
  * @file    modbus_app.h
  * @brief   Modbus register-map adapter.
  *
  *  Map of the device (12x discrete outputs). The DQ control/configuration
  *  block lives in holding registers 50..98 so it never overlaps the address
  *  ranges used by other module types (e.g. 12DI).
  *
  *    Input Registers (FC04):
  *      0..11   - DQ1..DQ12 output state, 0 or 1 (read-only echo)
  *      120     - firmware version major
  *      121     - firmware version minor
  *      122     - uptime, seconds (low word)
  *      123     - uptime, seconds (high word)
  *      124     - output state mask of DQ1..DQ12 as a 12-bit value
  *      125     - module ID (read-only, identifies hardware variant)
  *
  *    Holding Registers (FC03/FC06/FC10):
  *      50      - group output register: bit i (0..11) -> DQ(i+1) (R/W)
  *      51..62  - DQ1..DQ12 output value, 0/1 (R/W)
  *      63..74  - DQ1..DQ12 comms-loss mode: 0=HOLD, 1=ZERO, 2=SAFE
  *      75..86  - DQ1..DQ12 safe value, 0/1 (used by SAFE mode)
  *      87..98  - DQ1..DQ12 comms-loss timeout, x100 ms (0 = immediate)
  *      99      - reserved
  *      101     - LED mode (0 = ALW_OFF, 1 = ALW_ON, 2 = STATE_MACHINE)
  *      102     - Modbus slave id (informational on TCP)
  *      103     - Modbus TCP port
  *      104..107- static IPv4 octets (each register holds one octet)
  *      108..111- netmask octets
  *      112..115- gateway octets
  *      116     - use DHCP (0/1)
  *      117     - "save settings" trigger - write 0xA5A5 to commit to Flash
  *      118     - "reboot" trigger          - write 0xB00B to soft-reset,
  *                0xB007 to enter the bootloader, 0x8863 to hardware-reset
  *                the KSZ8863 Ethernet switch (recovery)
  *      119     - "factory reset" trigger   - write 0xDEAD to reload defaults
  *
  *  Output values and per-output configuration are committed to Flash by the
  *  "save settings" trigger and restored at power-on.
  ******************************************************************************
  */
#ifndef APPLICATION_MODBUS_APP_H
#define APPLICATION_MODBUS_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "nanomodbus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Magic write triggers (FC06/FC10 to specific holding registers). */
#define MODBUS_TRIG_SAVE            0xA5A5u
#define MODBUS_TRIG_REBOOT          0xB00Bu
#define MODBUS_TRIG_FACTORY_RESET   0xDEADu
/* Reboot into the bootloader (for OTA). Written to MB_HR_TRIG_REBOOT (118). */
#define MODBUS_TRIG_BOOTLOADER      0xB007u
/* Hardware-reset the KSZ8863 Ethernet switch (operator recovery command).
 * Written to MB_HR_TRIG_REBOOT (118). Interrupts pass-through traffic for
 * the duration of the chip reset + port auto-negotiation — use only when the
 * switch shows no signs of life. */
#define MODBUS_TRIG_SWITCH_RESET    0x8863u

/* No-init RAM cell shared with the bootloader: writing BOOT_REQUEST_MAGIC and
 * resetting makes the bootloader stay active. Address/magic MUST match the
 * bootloader (flash_map.h) and the RAM reservation in both linker scripts. */
#define BOOT_REQUEST_FLAG_ADDR      0x2001FFF0u
#define BOOT_REQUEST_MAGIC          0xB007CAFEu

/* Number of discrete outputs. */
#define MB_DQ_COUNT                 12u

/* Discrete-output control / configuration block (holding registers). */
#define MB_HR_DQ_GROUP              50u    /* group output mask (R/W)        */
#define MB_HR_DQ_VALUE_BASE         51u    /* 51..62  per-output value       */
#define MB_HR_DQ_MODE_BASE          63u    /* 63..74  per-output loss mode   */
#define MB_HR_DQ_SAFE_BASE          75u    /* 75..86  per-output safe value  */
#define MB_HR_DQ_TIMEOUT_BASE       87u    /* 87..98  per-output timeout     */

/* Holding register addresses, exposed for unit tests / introspection. */
#define MB_HR_LED_MODE              101u
#define MB_HR_SLAVE_ID              102u
#define MB_HR_TCP_PORT              103u
#define MB_HR_IP_BASE               104u
#define MB_HR_NETMASK_BASE          108u
#define MB_HR_GATEWAY_BASE          112u
#define MB_HR_USE_DHCP              116u
#define MB_HR_TRIG_SAVE             117u
#define MB_HR_TRIG_REBOOT           118u
#define MB_HR_TRIG_FACTORY_RESET    119u

/* Input register addresses. */
#define MB_IR_FW_VER_MAJOR          120u
#define MB_IR_FW_VER_MINOR          121u
#define MB_IR_UPTIME_LO             122u
#define MB_IR_UPTIME_HI             123u
#define MB_IR_DQ_MASK               124u
#define MB_IR_MODULE_ID             125u

/* Module ID values for MB_IR_MODULE_ID (register 125).
 * Select the active variant; comment out the rest. */
/* #define MODULE_ID_12DI  0x12D1u */  /* 12x DI                    */
#define MODULE_ID_12D0  0x12D0u  /* 12x DO — 12DQ/D4MG (this build) */
/* #define MODULE_ID_04RD  0x04DDu */  /* 4x Relay-DO               */
/* #define MODULE_ID_08A1  0x08A1u */  /* 8x AI variant 1           */
/* #define MODULE_ID_08A0  0x08A0u */  /* 8x AO variant 0           */

/** Initialise the modbus register adapter. Must be called after settings_init(). */
void modbus_app_init(void);

/** Mark a successful modbus transaction (used to drive STAT_LED state). */
void modbus_app_notify_request(void);

/** Get the populated nmbs_callbacks structure for nmbs_server_create(). */
const nmbs_callbacks* modbus_app_get_callbacks(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_MODBUS_APP_H */

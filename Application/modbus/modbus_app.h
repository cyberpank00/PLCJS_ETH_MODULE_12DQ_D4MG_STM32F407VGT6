/**
  ******************************************************************************
  * @file    modbus_app.h
  * @brief   Modbus register-map adapter.
  *
  *  Map of the device:
  *
  *    Discrete Inputs (FC02):
  *      0..11   - DI1..DI12 filtered state (read-only)
  *
  *    Input Registers (FC04):
  *      0..11   - DI1..DI12 filtered state, 0 or 1
  *      120     - firmware version major
  *      121     - firmware version minor
  *      122     - uptime, seconds (low word)
  *      123     - uptime, seconds (high word)
  *      124     - active poll mask of DI1..DI12 as a 12-bit value
  *
  *    Holding Registers (FC03/FC06/FC10):
  *      100     - DI filter time, ms (10..1000), default 50
  *      101     - LED mode (0 = ALW_OFF, 1 = ALW_ON, 2 = STATE_MACHINE)
  *      102     - Modbus slave id (informational on TCP)
  *      103     - Modbus TCP port
  *      104..107- static IPv4 octets (each register holds one octet)
  *      108..111- netmask octets
  *      112..115- gateway octets
  *      116     - use DHCP (0/1)
  *      117     - "save settings" trigger - write 0xA5A5 to commit to Flash
  *      118     - "reboot" trigger          - write 0xB00B to soft-reset
  *      119     - "factory reset" trigger   - write 0xDEAD to reload defaults
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

/* No-init RAM cell shared with the bootloader: writing BOOT_REQUEST_MAGIC and
 * resetting makes the bootloader stay active. Address/magic MUST match the
 * bootloader (flash_map.h) and the RAM reservation in both linker scripts. */
#define BOOT_REQUEST_FLAG_ADDR      0x2001FFF0u
#define BOOT_REQUEST_MAGIC          0xB007CAFEu

/* Holding register addresses, exposed for unit tests / introspection. */
#define MB_HR_DI_FILTER_MS          100u
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
#define MB_IR_DI_MASK               124u

#define MB_DI_COUNT                 12u

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

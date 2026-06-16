/**
  ******************************************************************************
  * @file    modbus_app.c
  * @brief   Modbus register-map adapter on top of the application modules.
  ******************************************************************************
  */

#include "modbus_app.h"

#include <string.h>

#include "cmsis_os.h"
#include "stm32f4xx_hal.h"

#include "di_module.h"
#include "led_module.h"
#include "settings.h"

/* ---------------------------------------------------------------------------
 * Firmware version
 * ------------------------------------------------------------------------- */
#define FW_VER_MAJOR    1u
#define FW_VER_MINOR    0u

/* ---------------------------------------------------------------------------
 * Pending action flags driven by special holding-register triggers.
 * Reading them back is allowed: they always read as 0.
 * ------------------------------------------------------------------------- */
static volatile uint8_t s_pending_save           = 0u;
static volatile uint8_t s_pending_reboot         = 0u;
static volatile uint8_t s_pending_factory_reset  = 0u;
static volatile uint8_t s_pending_bootloader     = 0u;
static volatile uint32_t s_last_request_tick     = 0u;

void modbus_app_init(void)
{
    s_pending_save          = 0u;
    s_pending_reboot        = 0u;
    s_pending_factory_reset = 0u;
    s_pending_bootloader    = 0u;
    s_last_request_tick     = 0u;
}

void modbus_app_notify_request(void)
{
    s_last_request_tick = HAL_GetTick();
}

uint8_t modbus_app_take_pending_save(void)
{
    uint8_t v = s_pending_save;
    s_pending_save = 0u;
    return v;
}

uint8_t modbus_app_take_pending_reboot(void)
{
    uint8_t v = s_pending_reboot;
    s_pending_reboot = 0u;
    return v;
}

uint8_t modbus_app_take_pending_factory_reset(void)
{
    uint8_t v = s_pending_factory_reset;
    s_pending_factory_reset = 0u;
    return v;
}

uint8_t modbus_app_take_pending_bootloader(void)
{
    uint8_t v = s_pending_bootloader;
    s_pending_bootloader = 0u;
    return v;
}

uint32_t modbus_app_last_request_tick(void)
{
    return s_last_request_tick;
}

/* ---------------------------------------------------------------------------
 * Helpers — apply a single holding-register write
 * ------------------------------------------------------------------------- */
static nmbs_error apply_holding_write(uint16_t address, uint16_t value)
{
    settings_t* s = settings_get();

    switch (address) {
    case MB_HR_DI_FILTER_MS:
        if (value < DI_FILTER_MS_MIN || value > DI_FILTER_MS_MAX) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }
        s->di_filter_ms = value;
        di_module_set_filter_ms(value);
        break;

    case MB_HR_LED_MODE:
        if (value > (uint16_t)LED_MODE_STATE_MACHINE) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }
        s->led_mode = value;
        led_module_set_mode((uint8_t)value);
        break;

    case MB_HR_SLAVE_ID:
        if (value < 1u || value > 247u) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }
        s->modbus_slave_id = (uint8_t)value;
        break;

    case MB_HR_TCP_PORT:
        if (value == 0u) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }
        s->modbus_tcp_port = value;
        break;

    case MB_HR_IP_BASE + 0u: case MB_HR_IP_BASE + 1u:
    case MB_HR_IP_BASE + 2u: case MB_HR_IP_BASE + 3u:
        if (value > 255u) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }
        s->ip[address - MB_HR_IP_BASE] = (uint8_t)value;
        break;

    case MB_HR_NETMASK_BASE + 0u: case MB_HR_NETMASK_BASE + 1u:
    case MB_HR_NETMASK_BASE + 2u: case MB_HR_NETMASK_BASE + 3u:
        if (value > 255u) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }
        s->netmask[address - MB_HR_NETMASK_BASE] = (uint8_t)value;
        break;

    case MB_HR_GATEWAY_BASE + 0u: case MB_HR_GATEWAY_BASE + 1u:
    case MB_HR_GATEWAY_BASE + 2u: case MB_HR_GATEWAY_BASE + 3u:
        if (value > 255u) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }
        s->gateway[address - MB_HR_GATEWAY_BASE] = (uint8_t)value;
        break;

    case MB_HR_USE_DHCP:
        if (value > 1u) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }
        s->use_dhcp = (uint8_t)value;
        break;

    case MB_HR_TRIG_SAVE:
        if (value == MODBUS_TRIG_SAVE) {
            s_pending_save = 1u;
        } else if (value != 0u) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }
        break;

    case MB_HR_TRIG_REBOOT:
        if (value == MODBUS_TRIG_REBOOT) {
            s_pending_reboot = 1u;
        } else if (value == MODBUS_TRIG_BOOTLOADER) {
            s_pending_bootloader = 1u;
        } else if (value != 0u) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }
        break;

    case MB_HR_TRIG_FACTORY_RESET:
        if (value == MODBUS_TRIG_FACTORY_RESET) {
            s_pending_factory_reset = 1u;
        } else if (value != 0u) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }
        break;

    default:
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    return NMBS_ERROR_NONE;
}

static uint16_t read_holding(uint16_t address)
{
    const settings_t* s = settings_get();

    switch (address) {
    case MB_HR_DI_FILTER_MS:    return s->di_filter_ms;
    case MB_HR_LED_MODE:        return s->led_mode;
    case MB_HR_SLAVE_ID:        return s->modbus_slave_id;
    case MB_HR_TCP_PORT:        return s->modbus_tcp_port;

    case MB_HR_IP_BASE + 0u:    return s->ip[0];
    case MB_HR_IP_BASE + 1u:    return s->ip[1];
    case MB_HR_IP_BASE + 2u:    return s->ip[2];
    case MB_HR_IP_BASE + 3u:    return s->ip[3];

    case MB_HR_NETMASK_BASE + 0u: return s->netmask[0];
    case MB_HR_NETMASK_BASE + 1u: return s->netmask[1];
    case MB_HR_NETMASK_BASE + 2u: return s->netmask[2];
    case MB_HR_NETMASK_BASE + 3u: return s->netmask[3];

    case MB_HR_GATEWAY_BASE + 0u: return s->gateway[0];
    case MB_HR_GATEWAY_BASE + 1u: return s->gateway[1];
    case MB_HR_GATEWAY_BASE + 2u: return s->gateway[2];
    case MB_HR_GATEWAY_BASE + 3u: return s->gateway[3];

    case MB_HR_USE_DHCP:           return s->use_dhcp;

    case MB_HR_TRIG_SAVE:
    case MB_HR_TRIG_REBOOT:
    case MB_HR_TRIG_FACTORY_RESET:
        return 0u;

    default:
        return 0u;
    }
}

static bool holding_address_valid(uint16_t address)
{
    if (address >= MB_HR_DI_FILTER_MS && address <= MB_HR_USE_DHCP) {
        return true;
    }
    if (address == MB_HR_TRIG_SAVE ||
        address == MB_HR_TRIG_REBOOT ||
        address == MB_HR_TRIG_FACTORY_RESET) {
        return true;
    }
    return false;
}

static uint16_t read_input(uint16_t address)
{
    if (address < MB_DI_COUNT) {
        return di_module_get_input((uint8_t)address) ? 1u : 0u;
    }
    switch (address) {
    case MB_IR_FW_VER_MAJOR:    return FW_VER_MAJOR;
    case MB_IR_FW_VER_MINOR:    return FW_VER_MINOR;
    case MB_IR_UPTIME_LO:       return (uint16_t)((HAL_GetTick() / 1000u) & 0xFFFFu);
    case MB_IR_UPTIME_HI:       return (uint16_t)(((HAL_GetTick() / 1000u) >> 16u) & 0xFFFFu);
    case MB_IR_DI_MASK:         return di_module_get_mask();
    case MB_IR_MODULE_ID:       return MODULE_ID_12DI;
    default:                    return 0u;
    }
}

/* ---------------------------------------------------------------------------
 * nanoMODBUS callbacks
 * ------------------------------------------------------------------------- */
static nmbs_error cb_read_discrete_inputs(uint16_t address, uint16_t quantity,
                                          nmbs_bitfield inputs_out)
{
    if ((uint32_t)address + quantity > MB_DI_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }
    nmbs_bitfield_reset(inputs_out);
    for (uint16_t i = 0; i < quantity; i++) {
        nmbs_bitfield_write(inputs_out, i,
                            di_module_get_input((uint8_t)(address + i)));
    }
    return NMBS_ERROR_NONE;
}

static nmbs_error cb_read_input_registers(uint16_t address, uint16_t quantity,
                                          uint16_t* registers_out)
{
    /* Allow read of DI block (0..11) and the metadata block (120..125). */
    for (uint16_t i = 0; i < quantity; i++) {
        const uint16_t a = (uint16_t)(address + i);
        if (a < MB_DI_COUNT || (a >= MB_IR_FW_VER_MAJOR && a <= MB_IR_MODULE_ID)) {
            registers_out[i] = read_input(a);
        } else {
            return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        }
    }
    return NMBS_ERROR_NONE;
}

static nmbs_error cb_read_holding_registers(uint16_t address, uint16_t quantity,
                                            uint16_t* registers_out)
{
    for (uint16_t i = 0; i < quantity; i++) {
        const uint16_t a = (uint16_t)(address + i);
        if (!holding_address_valid(a)) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        }
        registers_out[i] = read_holding(a);
    }
    return NMBS_ERROR_NONE;
}

static nmbs_error cb_write_single_register(uint16_t address, uint16_t value)
{
    if (!holding_address_valid(address)) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }
    return apply_holding_write(address, value);
}

static nmbs_error cb_write_multiple_registers(uint16_t address, uint16_t quantity,
                                              const uint16_t* registers)
{
    /* Validate range first so partial application is avoided. */
    for (uint16_t i = 0; i < quantity; i++) {
        if (!holding_address_valid((uint16_t)(address + i))) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        }
    }
    for (uint16_t i = 0; i < quantity; i++) {
        const nmbs_error e = apply_holding_write((uint16_t)(address + i), registers[i]);
        if (e != NMBS_ERROR_NONE) {
            return e;
        }
    }
    return NMBS_ERROR_NONE;
}

static const nmbs_callbacks s_callbacks = {
    .read_coils                = NULL,
    .read_discrete_inputs      = cb_read_discrete_inputs,
    .read_holding_registers    = cb_read_holding_registers,
    .read_input_registers      = cb_read_input_registers,
    .write_single_coil         = NULL,
    .write_single_register     = cb_write_single_register,
    .write_multiple_coils      = NULL,
    .write_multiple_registers  = cb_write_multiple_registers,
};

const nmbs_callbacks* modbus_app_get_callbacks(void)
{
    return &s_callbacks;
}

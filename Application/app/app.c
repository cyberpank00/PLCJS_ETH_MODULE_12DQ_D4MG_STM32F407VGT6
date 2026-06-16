/**
  ******************************************************************************
  * @file    app.c
  * @brief   Application orchestrator. Wires the modular drivers together,
  *          handles factory-reset, brings up the network interface in the
  *          configured mode and runs the housekeeping loop (LED ticks,
  *          watchdog, deferred Modbus actions).
  ******************************************************************************
  */

#include "app.h"
#include <string.h>

#include "cmsis_os.h"
#include "iwdg.h"
#include "lwip/api.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "main.h"
#include "stm32f4xx_hal.h"

#include "button_module.h"
#include "di_module.h"
#include "ksz8863.h"
#include "led_module.h"
#include "modbus_app.h"
#include "modbus_tcp_server.h"
#include "settings.h"

/* The LwIP MX_LWIP_Init() exposes its struct netif so that we can override
 * the addressing after MX_LWIP_Init() has run. */
extern struct netif gnetif;
extern ETH_HandleTypeDef heth;

/* Diagnostic counters from ethernetif.c */
extern volatile uint32_t ethernetif_rx_int_cnt;
extern volatile uint32_t ethernetif_rx_frame_cnt;
extern volatile uint32_t ethernetif_tx_frame_cnt;
extern volatile uint32_t ethernetif_tx_fail_cnt;

/* Physical link state from ethernet_link_thread (ethernetif.c). */
extern volatile uint8_t g_eth_any_link_up;

/* Module accessors implemented in modbus_app.c. */
uint8_t modbus_app_take_pending_save(void);
uint8_t modbus_app_take_pending_reboot(void);
uint8_t modbus_app_take_pending_factory_reset(void);
uint8_t modbus_app_take_pending_bootloader(void);
uint32_t modbus_app_last_request_tick(void);

/* Latest result of the boot-time KSZ8863 SMI self-test. Exposed as a flag
 * for any module that wants to react to the switch being unreachable
 * (e.g. an LED pattern, future Modbus diagnostic registers). */
static bool     s_ksz8863_present = false;
static uint16_t s_ksz8863_id1     = 0u;
static uint16_t s_ksz8863_id2     = 0u;

/* ----------- Debug diagnostics (inspect via debugger) -------------------- */
volatile struct {
    uint8_t  settings_from_flash;  /* 1 = loaded from Flash, 0 = defaults    */
    uint8_t  use_dhcp;             /* effective use_dhcp value                */
    uint8_t  ip[4];                /* effective IP                            */
    uint8_t  netmask[4];           /* effective netmask                       */
    uint8_t  gateway[4];           /* effective gateway                       */
    uint8_t  ksz_present;          /* 1 = KSZ8863 answered self-test          */
    uint16_t ksz_id1;              /* PHY ID1 readback                        */
    uint16_t ksz_id2;              /* PHY ID2 readback                        */
    uint8_t  netif_up;             /* gnetif.flags & NETIF_FLAG_UP            */
    uint8_t  netif_link_up;        /* gnetif.flags & NETIF_FLAG_LINK_UP       */
    uint32_t netif_ip;             /* gnetif.ip_addr as u32                   */
    uint8_t  boot_stage;           /* increments at each init step            */
    uint32_t ksz_chipid;           /* KSZ8863 reg 0x00 (ChipID0) via SMI     */
    uint32_t ksz_gc4;              /* KSZ8863 Global Control 4 (reg 0x06)    */
    uint32_t ksz_gc4_after;        /* GC4 after RMII bit set                 */
    uint32_t eth_dmasr;            /* ETH DMA Status Register snapshot       */
    uint32_t eth_maccr;            /* ETH MAC Config Register snapshot       */
    uint8_t  eth_start_ok;         /* 1 = HAL_ETH_Start_IT succeeded         */
    uint32_t mmc_tx_good;          /* MMC: TX good frames                    */
    uint32_t mmc_rx_good_uni;      /* MMC: RX good unicast frames            */
    uint32_t mmc_rx_crc_err;       /* MMC: RX CRC error frames               */
    uint32_t mmc_rx_align_err;     /* MMC: RX alignment error frames         */
    uint32_t port1_bmsr;           /* KSZ port 1 BMSR (link status)          */
    uint32_t port2_bmsr;           /* KSZ port 2 BMSR (link status)          */
    uint32_t rx_int_cnt;           /* ETH RX interrupts fired                */
    uint32_t rx_frame_cnt;         /* frames passed to LwIP                  */
    uint32_t tx_frame_cnt;         /* frames sent by LwIP                    */
    uint32_t tx_fail_cnt;          /* TX errors                              */
    uint8_t  phy_link_up;          /* any KSZ8863 external port has link     */
    uint8_t  save_ok;              /* last settings_save() result            */
    uint8_t  save_count;           /* total settings_save() calls            */
} dbg;

/* ---------------------------------------------------------------------------
 * Sub-tasks
 * ------------------------------------------------------------------------- */

/* DI sampling task: 1 ms period, drives the anti-bounce filter. */
static void di_task(void* arg)
{
    (void)arg;
    uint32_t tick = osKernelGetTickCount();
    for (;;) {
        di_module_tick();
        tick += 1u;
        osDelayUntil(tick);
    }
}

/* LED state machine: 10 ms tick. */
static void led_task(void* arg)
{
    (void)arg;
    const uint16_t period_ms = 10u;
    uint32_t tick = osKernelGetTickCount();
    for (;;) {
        led_module_tick(period_ms);
        tick += period_ms;
        osDelayUntil(tick);
    }
}

/* ---------------------------------------------------------------------------
 * Factory reset routine
 *
 * Order matters: commit defaults to Flash first (HAL_FLASH erase of sector 11
 * blocks the CPU and prevents the LED task from running for ~1-2 s), then
 * play the visual confirmation burst, then reboot. The LED task must already
 * be running when this routine is entered (see app_run()).
 * ------------------------------------------------------------------------- */
static void perform_factory_reset(void)
{
    /* Reset and persist defaults to Flash. */
    settings_reset_to_defaults();
    dbg.save_ok = settings_save() ? 1u : 0u;
    dbg.save_count++;

    /* Enter the sticky LED_STATE_FACTORY_RESET — the LED task drives the
     * configurable periodic ON/OFF blink on its 10 ms tick until the reboot
     * below. Default cadence is 100 ms ON / 100 ms OFF, configurable via
     * led_module_set_factory_reset_timing(). */
    led_module_signal_factory_reset();

    /* 3.5 s wait — long enough for the operator to see the indication at
     * the default 5 Hz cadence (and reasonable headroom at slower settings).
     * The LED keeps blinking the whole time. */
    const uint32_t deadline = HAL_GetTick() + 3500u;
    while (HAL_GetTick() < deadline) {
        HAL_IWDG_Refresh(&hiwdg);
        dbg.netif_up      = (gnetif.flags & NETIF_FLAG_UP)      ? 1u : 0u;
        dbg.netif_link_up = (gnetif.flags & NETIF_FLAG_LINK_UP) ? 1u : 0u;
        dbg.netif_ip      = gnetif.ip_addr.addr;
        dbg.boot_stage    = 4;
        osDelay(50);
    }
    NVIC_SystemReset();
}

/* ---------------------------------------------------------------------------
 * Network bring-up
 * ------------------------------------------------------------------------- */
static void apply_network_config(void)
{
    const settings_t* s = settings_get();

    if (s->use_dhcp) {
        /* MX_LWIP_Init() already started DHCP. Nothing more to do. */
        return;
    }

    /* Static configuration: stop DHCP if it happens to be running and load
     * the static address. */
    dhcp_release_and_stop(&gnetif);

    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip,   s->ip[0],      s->ip[1],      s->ip[2],      s->ip[3]);
    IP4_ADDR(&mask, s->netmask[0], s->netmask[1], s->netmask[2], s->netmask[3]);
    IP4_ADDR(&gw,   s->gateway[0], s->gateway[1], s->gateway[2], s->gateway[3]);

    netif_set_addr(&gnetif, &ip, &mask, &gw);
}

/* ---------------------------------------------------------------------------
 * Housekeeping
 * ------------------------------------------------------------------------- */
static void update_led_state_from_traffic(void)
{
    /* No physical link on either KSZ8863 port → distinct pattern. */
    if (!g_eth_any_link_up) {
        led_module_set_state(LED_STATE_NO_LINK);
        return;
    }

    const uint32_t now      = HAL_GetTick();
    const uint32_t last_req = modbus_app_last_request_tick();

    /* "Polling" if a Modbus client is currently connected AND we have seen
     * a request in the last 5 s. Otherwise "no polling". */
    extern bool modbus_tcp_server_has_client(void);
    const bool has_client    = modbus_tcp_server_has_client();
    const bool recent_traffic = (last_req != 0u) && ((now - last_req) <= 5000u);

    led_module_set_state((has_client && recent_traffic)
                         ? LED_STATE_POLLING
                         : LED_STATE_NO_POLLING);
}

/* ---------------------------------------------------------------------------
 * Public entry point
 * ------------------------------------------------------------------------- */
void app_run(void)
{
    /* Load settings (or defaults) before anything that consumes them. */
    dbg.boot_stage = 1;
    dbg.settings_from_flash = settings_init() ? 1u : 0u;
    settings_t* s = settings_get();
    dbg.use_dhcp = s->use_dhcp;
    memcpy(dbg.ip, s->ip, 4);
    memcpy(dbg.netmask, s->netmask, 4);
    memcpy(dbg.gateway, s->gateway, 4);

    /* Initialise the LED module and spawn its task BEFORE the button check.
     * The factory-reset burst (10 short blinks) is driven by led_module_tick()
     * which runs from led_task; if the task is not yet running, the burst is
     * silently dropped. */
    led_module_init((uint8_t)s->led_mode);
    const osThreadAttr_t led_attr_early = {
        .name = "LED", .stack_size = 256, .priority = osPriorityLow
    };
    osThreadNew(led_task, NULL, &led_attr_early);

    /* If the FACT_RES button is held at startup, blank-load defaults. We do
     * this synchronously here so that the rest of the boot uses defaults. */
    if (button_wait_held(BUTTON_HOLD_FOR_FACTORY_RESET_MS)) {
        perform_factory_reset();
        /* Not reached. */
    }

    /* Initialise the remaining hardware drivers. */
    di_module_init(s->di_filter_ms);
    modbus_app_init();

    /* Apply network configuration (static or DHCP). */
    apply_network_config();
    dbg.boot_stage = 2;

    /* HAL_ETH_Init() has already run by the time we get here (LwIP init
     * called it from low_level_init()), so SMI/MIIM access is available.
     * Probe the switch once: if it answers with a Micrel OUI we know the
     * RMII/MDIO bus is healthy. The result is kept in static state for
     * other modules to consult. */
    s_ksz8863_present = ksz8863_self_test(&s_ksz8863_id1, &s_ksz8863_id2);
    dbg.ksz_present = s_ksz8863_present ? 1u : 0u;
    dbg.ksz_id1     = s_ksz8863_id1;
    dbg.ksz_id2     = s_ksz8863_id2;
    dbg.boot_stage  = 3;

    /* --- KSZ8863 indirect register diagnostics (SMI) ---
     * Register address = {PHY_ADDR[2:0], REG_ADDR[4:0]}.
     * Reg 0x00 (ChipID0): PHY 0, reg 0.  Reg 0x06 (GC4): PHY 0, reg 6. */
    {
        uint32_t tmp = 0;
        HAL_ETH_ReadPHYRegister(&heth, 0, 0, &tmp);  /* ChipID0 */
        dbg.ksz_chipid = tmp;

        HAL_ETH_ReadPHYRegister(&heth, 0, 6, &tmp);  /* Global Control 4 */
        dbg.ksz_gc4 = tmp;

        /* Bit 6 of GC4: 0=MII, 1=RMII for port 3.  Force RMII if needed. */
        if ((tmp & 0x0040u) == 0u) {
            tmp |= 0x0040u;
            HAL_ETH_WritePHYRegister(&heth, 0, 6, tmp);
        }
        HAL_ETH_ReadPHYRegister(&heth, 0, 6, &tmp);
        dbg.ksz_gc4_after = tmp;

        /* Snapshot MAC/DMA state */
        dbg.eth_dmasr = ETH->DMASR;
        dbg.eth_maccr = ETH->MACCR;
    }

    /* Spawn the DI sampling task. The LED task was started earlier so that
     * the factory-reset burst is visible during the boot-time button-hold
     * path (see above). */
    const osThreadAttr_t di_attr  = {
        .name = "DI", .stack_size = 384, .priority = osPriorityHigh
    };
    osThreadNew(di_task, NULL, &di_attr);

    /* Spawn Modbus TCP server. */
    modbus_tcp_server_start();

    /* Housekeeping loop: feeds the watchdog, processes deferred actions and
     * keeps the LED state in sync with the Modbus traffic. */
    uint32_t tick = osKernelGetTickCount();
    for (;;) {
        HAL_IWDG_Refresh(&hiwdg);
        dbg.netif_up      = (gnetif.flags & NETIF_FLAG_UP)      ? 1u : 0u;
        dbg.netif_link_up = (gnetif.flags & NETIF_FLAG_LINK_UP) ? 1u : 0u;
        dbg.netif_ip      = gnetif.ip_addr.addr;
        dbg.eth_dmasr     = ETH->DMASR;
        dbg.eth_maccr     = ETH->MACCR;
        dbg.eth_start_ok  = (heth.gState == HAL_ETH_STATE_STARTED) ? 1u : 0u;
        dbg.mmc_tx_good      = ETH->MMCTGFCR;
        dbg.mmc_rx_good_uni  = ETH->MMCRGUFCR;
        dbg.mmc_rx_crc_err   = ETH->MMCRFCECR;
        dbg.mmc_rx_align_err = ETH->MMCRFAECR;
        { uint32_t v = 0;
          HAL_ETH_ReadPHYRegister(&heth, 1, 1, &v); dbg.port1_bmsr = v;
          HAL_ETH_ReadPHYRegister(&heth, 2, 1, &v); dbg.port2_bmsr = v;
        }
        dbg.rx_int_cnt   = ethernetif_rx_int_cnt;
        dbg.rx_frame_cnt = ethernetif_rx_frame_cnt;
        dbg.tx_frame_cnt = ethernetif_tx_frame_cnt;
        dbg.tx_fail_cnt  = ethernetif_tx_fail_cnt;
        dbg.phy_link_up  = g_eth_any_link_up;
        dbg.boot_stage    = 4;
        update_led_state_from_traffic();

        if (modbus_app_take_pending_save()) {
            HAL_IWDG_Refresh(&hiwdg);
            dbg.save_ok = settings_save() ? 1u : 0u;
            dbg.save_count++;
        }
        if (modbus_app_take_pending_factory_reset()) {
            perform_factory_reset();
            /* Not reached. */
        }
        if (modbus_app_take_pending_bootloader()) {
            /* Ask the bootloader to stay active after the reset: write the
             * shared magic into the no-init RAM cell, then reset. The cell
             * survives a warm reset; the bootloader consumes it on entry. */
            *(volatile uint32_t *)BOOT_REQUEST_FLAG_ADDR = BOOT_REQUEST_MAGIC;
            const uint32_t bl_deadline = HAL_GetTick() + 200u;
            while (HAL_GetTick() < bl_deadline) {
                HAL_IWDG_Refresh(&hiwdg);
                osDelay(20);
            }
            NVIC_SystemReset();
        }
        if (modbus_app_take_pending_reboot()) {
            const uint32_t deadline = HAL_GetTick() + 200u;
            while (HAL_GetTick() < deadline) {
                HAL_IWDG_Refresh(&hiwdg);
        dbg.netif_up      = (gnetif.flags & NETIF_FLAG_UP)      ? 1u : 0u;
        dbg.netif_link_up = (gnetif.flags & NETIF_FLAG_LINK_UP) ? 1u : 0u;
        dbg.netif_ip      = gnetif.ip_addr.addr;
        dbg.boot_stage    = 4;
                osDelay(20);
            }
            NVIC_SystemReset();
        }

        tick += 100u;
        osDelayUntil(tick);
    }
}

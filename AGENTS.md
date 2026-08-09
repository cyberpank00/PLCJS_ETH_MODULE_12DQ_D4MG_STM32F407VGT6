# AGENTS.md

Firmware for the PLCJS Ethernet 12DQ module (12 discrete outputs, STM32F407VGT6,
KSZ8863 switch, Modbus TCP). This file is the orientation map for agents; it
records architecture, invariants and gotchas. User-facing documentation (register
tables, wiring, electrical limits) lives in `README.md` / `README_EN.md`.

## Build (CMake)

Toolchain: STM32 Arm Clang (`starm-clang`) from STM32CubeCLT, generator Ninja.
Toolchain file: `cmake/starm-clang.cmake`. Presets in `CMakePresets.json`.

Configure + build (Debug):

```
cmake --preset Debug
cmake --build --preset Debug
```

Release:

```
cmake --preset Release
cmake --build --preset Release
```

Build output: `build/<preset>/PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6.{elf,hex,bin,map}`.

Clean rebuild: delete `build/<preset>` and re-run the configure step.

There are no host-side unit tests. "Verified" means: it compiles, and where the
change is observable it was exercised against hardware with the scripts in
`Tools/` (see *Testing*).

### Tools required
- CMake >= 3.22
- Ninja
- `starm-clang` (STM32CubeCLT) on PATH. Alternative GCC toolchain available at `cmake/gcc-arm-none-eabi.cmake`.

## Repository layout

| Path | Owner | Notes |
|---|---|---|
| `Application/` | hand-written | All real logic. Edit here. |
| `Core/`, `Drivers/`, `Middlewares/`, `LWIP/`, `cmake/stm32cubemx/` | STM32CubeMX | Regenerated from the `.ioc`. |
| `startup_stm32f407xx.s`, `STM32F407XX_FLASH.ld` | hand-edited | Diverged from CubeMX output — see *Linker*. |
| `Tools/*.mjs` | hand-written | Node hardware test scripts, not built. |
| `DOC/` | assets | Schematic PDF, product photos. |

**CubeMX regeneration hazard.** Re-running code generation from
`PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6.ioc` overwrites `Core/`, `Drivers/`,
`Middlewares/`, `LWIP/` and `cmake/stm32cubemx/CMakeLists.txt`. Several of these
carry hand edits outside `USER CODE` guards — notably `LWIP/Target/ethernetif.c`
(link polling, `g_eth_any_link_up`) and `LWIP/Target/lwipopts.h`. Diff carefully
after any regeneration; do not regenerate casually.

## Module map (`Application/`)

| Module | Responsibility |
|---|---|
| `app/` | Orchestrator. Boot order, factory reset, network bring-up, housekeeping loop. Start reading here. |
| `dq/` | 12-channel discrete output driver, active-high push-pull, per-channel comms-loss behaviour. |
| `modbus/modbus_app.c` | Register-map adapter (nanoMODBUS callbacks). **The register map is documented in the header comment of `modbus_app.h`.** |
| `modbus/modbus_tcp_server.c` | Single-client TCP server on LwIP netconn; newest connection wins. |
| `settings/` | Flash-backed settings, CRC32-protected, magic + version. |
| `discovery/` | PDP responder, UDP/20556 broadcast. Find/address a device by MAC without an IP. |
| `net_id/` | Derives MAC and link-local IPv4 from the 96-bit MCU UID. |
| `ksz8863/` | SMI/MIIM driver for the Ethernet switch; reset policy and recovery. |
| `led/` | STAT_LED state machine, 10 ms tick. |
| `button/` | FACT_RES button (PE10, active-low), boot-time hold detection. |
| `fw_header/` | Firmware image header consumed by the bootloader. Module identity lives here. |
| `third_party/nanomodbus/` | Vendored protocol library, locally patched. |

## Invariants

Violating these produces bugs that only show up on hardware or during OTA.

### Single sources of truth
- **Module identity** — `Application/fw_header/fw_header.h`:
  `FW_PRODUCT_ID = 0x504C1202`, `FW_HW_REVISION = 0x0101`, `FW_VERSION_VALUE = 0x0107`.
  Bump `FW_VERSION_VALUE` here and nowhere else. `CMakeLists.txt` deliberately
  passes no identity defines.
- **Firmware version over Modbus** — IR120/IR121 are derived from
  `FW_VERSION_VALUE`. Never hardcode a version in `modbus_app.c`.
- **Register map** — the header comment of `modbus_app.h`, mirrored by the
  `MB_HR_*` / `MB_IR_*` constants. Keep comment and constants in step.
- **Module ID** — `MODULE_ID_12D0 = 0x12D0` in `modbus_app.h`, reported in IR125.

### Version policy — bump the minor on every change

**Mandatory.** Every change to firmware behaviour ships with `FW_VERSION_VALUE`
in `Application/fw_header/fw_header.h` incremented by one minor
(`0x0107` → `0x0108`). The version is the operator's only way to tell which
build is running on a device in the field, so an un-bumped change is a defect.

- Minor bump: any firmware-only change — fixes, features, register-map
  additions, timing changes.
- Major bump: only together with a `FW_HW_REVISION` major change (MCU pinout).
  OTA requires `fw_version` major == `hw_revision` major.
- Pure documentation-only commits (no compiled change) do not need a bump.

Bump checklist — all three places, they drift easily:
1. `FW_VERSION_VALUE` in `fw_header.h`.
2. Version rows in `README.md` (both the `fw_version` image row and the
   IR120/IR121 row).
3. The same rows in `README_EN.md`.

A chronological version-review / changelog file is planned; once it exists, add
an entry there in the same commit as the bump.

### Persistence
- `settings_t` layout is frozen. Reordering or resizing fields requires bumping
  `SETTINGS_VERSION` (currently 2); a mismatch makes deployed units silently
  fall back to factory defaults (link-local), which looks like a field failure.
- The field is still named `use_dhcp` but holds a tri-state net mode
  (`NET_MODE_STATIC/DHCP/LINKLOCAL`). Kept for on-flash compatibility — do not
  "clean this up".
- Settings live in **sector 10 @ `0x080C0000`** (`settings.c`). Sector 11 is
  bootloader staging. The file-header comment in `settings.c` still says
  "sector 11 / 0x080E0000" — the comment is stale, the code is right.
- Output state is committed to Flash only by the save trigger (HR117), not on
  every write.

### Threading
- All KSZ8863 SMI access must stay on the link-polling thread.
  `ksz8863_request_recovery()` only sets a flag; `ksz8863_service()` performs the
  reset. Never call `ksz8863_hw_reset()` from a Modbus or housekeeping context.
- LwIP calls must run in the tcpip thread. The live network re-apply goes through
  `tcpip_callback(apply_network_config_tcpip, ...)`.
- Flash writes and resets requested over Modbus/discovery are deferred to the
  housekeeping loop in `app_run()` via the `*_take_pending_*()` flags, so they
  never run inside the tcpip thread.
- Any loop that blocks longer than the IWDG period must call
  `HAL_IWDG_Refresh(&hiwdg)` — see the wait loops in `app.c`.

### Boot order (`app_run()`)
`settings_init()` → `led_module_init()` + spawn LED task → FACT_RES check →
`dq_module_init()` → `modbus_app_init()` → `apply_network_config()` →
`ksz8863_ensure_rmii_port3()` → `modbus_tcp_server_start()` → `discovery_init()` →
housekeeping loop (100 ms).

Two ordering constraints, both load-bearing:
- The LED task starts **before** the button check, otherwise the factory-reset
  blink is silently dropped.
- `perform_factory_reset()` writes Flash **before** the visual confirmation: the
  sector erase blocks the CPU for ~1–2 s and would freeze the blink.

## Gotchas

- **Never reset the KSZ8863 on a warm reboot.** The switch forwards traffic
  between ports 1 and 2 autonomously, so it must survive an MCU reset — a reset
  drops both external links for seconds of auto-negotiation, and this module may
  be mid-chain. Only cold boot (power-on/brown-out) resets it defensively, plus
  explicit recovery (HR118 = `0x8863`, or ~5 s of dead SMI). See
  `ksz8863_boot_init()`.
- `ksz8863_hw_reset()` runs before `HAL_ETH_Init()`; every other KSZ8863 API
  needs SMI up, i.e. after `MX_LWIP_Init()`.
- **Device name is 15 chars + NUL in a fixed 16-byte field.** The PDP IDENTIFY
  response is a fixed 38 bytes. This must stay identical across every module
  variant and ModbusTool, or discovery breaks between products.
- Modbus TCP is single-client, newest-wins: a new connection drops the old one.
  A hung client therefore cannot lock the device out.
- `LED_STATE_FACTORY_RESET` is sticky until reboot and overrides all other
  states and modes.
- A DQ comms-loss action latches; the latch clears only on the next explicit
  command to that channel.
- HR118 multiplexes distinct magics: `0xB00B` reboot, `0xB007` bootloader,
  `0x8863` switch reset. HR117 = `0xA5A5` save, HR119 = `0xDEAD` factory reset.
- Saving settings re-applies network config live — IP/DHCP changes take effect
  without a reboot.

## Linker / memory contract with the bootloader

`STM32F407XX_FLASH.ld` is **not** a stock CubeMX script:

- `FLASH` origin is `0x08040000`, length 256 K — the application slot, not
  `0x08000000`. The image only runs via the bootloader.
- `RAM` length is `0x1FFF0`, not 128 K. The top 16 bytes are reserved for the
  no-init boot-request cell at `0x2001FFF0` (`BOOT_REQUEST_MAGIC = 0xB007CAFE`),
  which the app writes before resetting into the bootloader.
- `.fw_header` is padded to offset `0x200` from the start of FLASH. The
  bootloader reads the header at exactly `APP_FLASH_BASE + 0x200`.

`fw_header_t` must stay byte-identical to `fw_header_t` in the bootloader's
`Application/validate/app_validate.h` (28 bytes, packed). CRC32 and image size
are *not* in the header — they travel in OTA metadata.

Flash map (owned by the bootloader's `Application/flash/flash_map.h`):
sectors 0–4 bootloader, 5 metadata, 6–7 application, 8–9 staging, 10 app
settings, 11 staging. Max image 256 KB.

OTA acceptance: `product_id` exact match **and** `hw_revision` major byte match.

## Testing

Hardware scripts in `Tools/` (Node 18+, no dependencies, plain `node:net`):

| Script | Stand | What it catches |
|---|---|---|
| `dq12_relay_stress_test.mjs` | 12DO relay contacts wired into a 12DI module | Stuck / mis-wired / non-switching channels, real relay propagation time. Self-calibrates mapping and polarity at startup. |
| `dq_random_stress_test.mjs` | 12DO alone, inductive loads | Back-EMF latch-up, dropped links, MCU resets/brown-outs (watches uptime IR122/123 for drops). |
| `dq_di_loopback_test.mjs` | DQ0 → DI0 single wire | Signal-path latency distribution. |

Defaults assume DO at `.11` and DI at `.10`; override with `--do-ip` / `--di-ip`.
All accept `--seconds` for a short smoke run instead of the default long soak.

Run a short soak after touching `dq_module.c`, the Modbus write path or anything
timing-related.

## Multi-repo workspace

Sibling repos under `E:\STM_Programming\`, all on `main`:

| Repo | Role |
|---|---|
| `PLCJS_ETH_MODULE_12DQ_D4MG_...` | This module — 12 discrete outputs, `0x504C1202` / IR125 `0x12D0`. |
| `PLCJS_ETH_MODULE_12DI_D4MG_...` | 12 discrete inputs, `0x504C1201` / `0x12D1`. Closest sibling; most shared code originates or lands here. |
| `PLCJS_ETH_MODULE_4RTD_D4MG_...` | 4x RTD, `0x504C0403` / `0x04D1`. |
| `BOOTLOADER_PLCJS_ETH_MODULE_12DI_D4MG_...` | Shared bootloader. Owns `flash_map.h`, `app_validate.h`, `scripts/variants.csv`. |
| `PLCJS_Module_ModbusTool` | Qt6/C++17 desktop client (Windows). Register maps in `src/maps/ModuleMaps.cpp`, PDP in `src/protocol/Pdp.cpp`. |

**`Application/` subsystems are copy-pasted between firmware variants, not
shared via a submodule.** `discovery/`, `net_id/`, `settings/`, `ksz8863/`,
`fw_header/`, `led/`, `button/` and the vendored `nanomodbus/` all exist
independently in each repo and have already diverged. A fix in a shared
subsystem here is **not** fixed elsewhere — say so explicitly rather than
implying a repo-wide fix.

Cross-repo contracts that must be changed in lockstep:
- **Wire format** (PDP frame layout, 38-byte IDENTIFY, 16-byte name) — every
  firmware + `Pdp.cpp` in ModbusTool.
- **`fw_header_t` layout, `FW_HEADER_OFFSET`, `BOOT_REQUEST_FLAG_ADDR`/`MAGIC`,
  flash map** — every firmware + bootloader + both linker scripts.
- **product_id** — `fw_header.h` here and `scripts/variants.csv` in the
  bootloader. (Note `variants.csv` uses the 3-byte hw encoding `0x010101` while
  firmware headers use the 2-byte `0x0101`; the bootloader compares major only.)
- **Register map changes** — `modbus_app.h` here and `ModuleMaps.cpp` in
  ModbusTool, or the tool shows stale registers.

Parity status, as of this writing: 12DI, 12DQ and 4RTD are aligned on the shared
subsystems — `ksz8863.c/h`, `nanomodbus.c`, `discovery.c`, `net_id.c`,
`button_module.c` and `modbus_tcp_server.c` are byte-identical across all three.
4RTD was brought up to date (KSZ8863 cold-boot policy + recovery service, HR118
`0x8863`, FC15/FC16 hardening) and bumped to fw 1.2. Module-specific files
(`led_module`, `settings`, `ethernetif` hostname) differ by design. Still diff a
shared file before assuming it matches — nothing enforces this automatically.

Flash-sector conflict to respect: 4RTD stores its write-once calibration in
**sector 11 @ `0x080E0000`**, which the bootloader's `flash_map.h` nominally
lists as a third staging sector (currently unused — staging is sectors 8–9 only).
If the bootloader is ever extended to use sector 11 for staging, it will destroy
4RTD calibration data.

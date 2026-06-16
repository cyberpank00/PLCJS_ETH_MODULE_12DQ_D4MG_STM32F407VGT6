# PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6

Main firmware for the PLCJS Ethernet 12-DQ module based on `STM32F407VGT6`.

The application drives 12 discrete outputs over `Modbus TCP` (Modbus coils), stores settings in
internal Flash, drives `STAT_LED`, supports factory reset, and can hand over control to the
Ethernet bootloader for OTA updates.

## Origin

A port of [`PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6`](https://github.com/cyberpank00/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6):
the 12 discrete **inputs** are replaced with 12 discrete **outputs**. All networking
infrastructure (KSZ8863/RMII, LwIP, Modbus TCP server, Flash settings, STAT_LED, FACT_RES,
bootloader hand-over) is kept unchanged.

## Current status

- `CMake + Ninja` build (Linux syntax-check with `arm-none-eabi-gcc`, target build with `starm-clang`)
- **not yet validated on real 12DQ hardware** — the output pin map and polarity must be confirmed
  against the 12DQ board schematic (see "Discrete outputs")

## Hardware platform

| Block | Description |
|---|---|
| MCU | `STM32F407VGT6`, Cortex-M4F |
| Ethernet | `KSZ8863` Ethernet switch/PHY, RMII |
| Discrete outputs | 12 outputs, push-pull, configurable polarity (`DQ_ACTIVE_HIGH`) |
| Factory reset | `FACT_RES` button, active-low |
| Indication | `STAT_LED`, active-high |
| Watchdog | `IWDG`, refreshed from the application housekeeping loop |

## Flash and RAM layout

The application image is linked to run with the bootloader.

| Region | Address | Size | Purpose |
|---|---:|---:|---|
| Bootloader | `0x08000000` | 128 KB | sectors 0-4, separate project |
| Metadata | `0x08020000` | 128 KB | sector 5, OTA state |
| Application | `0x08040000` | 256 KB | sectors 6-7, this firmware |
| Staging | `0x08080000` | 256 KB | sectors 8-9, OTA staging |
| Settings | `0x080C0000` | 128 KB | sector 10, application settings |

RAM:

- main RAM starts at `0x20000000`
- top 16 bytes are reserved for the shared boot-request flag
- shared flag address: `0x2001FFF0`
- shared flag magic: `0xB007CAFE`

## Build

STM32CubeCLT with `starm-clang`, `cmake`, and `ninja` is required.

```powershell
cmake -S . -B build/Debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake
cmake --build build/Debug
```

Main build outputs:

- `build/Debug/PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6.elf`
- `build/Debug/PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6.hex`
- `build/Debug/PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6.bin`
- `build/Debug/app.bin` - OTA image for the bootloader

## Flashing

Via ST-Link:

```powershell
STM32_Programmer_CLI -c port=SWD -w build/Debug/PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6.elf -v -rst
```

For bootloader OTA, use `build/Debug/app.bin` and `tools/fw_update.py` from the bootloader repository.

## Network

Network settings are stored in Flash and exposed via Modbus holding registers.

Defaults:

| Parameter | Value |
|---|---|
| DHCP | `1` / enabled |
| Static IP | `192.168.142.147` |
| Netmask | `255.255.255.0` |
| Gateway | `192.168.142.1` |
| Modbus TCP port | `502` |
| Modbus unit id | `1` |

Notes:

- `USE_DHCP = 1` means the static IP is stored but not applied
- IP/port/DHCP changes take effect after `TRIG_SAVE` and `TRIG_REBOOT`

## Discrete outputs

Outputs are push-pull. A logical `1` drives the MCU pin HIGH by default
(`DQ_ACTIVE_HIGH = 1` in `Application/dq/dq_module.h`); polarity can be inverted with a single macro.

> The pin map below is inherited from the 12DI board (the same pins switched to output mode).
> Confirm it against the 12DQ board schematic before flashing real hardware.

| Modbus coil | Silkscreen | MCU pin | Mask bit |
|---:|---|---|---:|
| 0 | DQ1 | PB3 | `0x001` |
| 1 | DQ2 | PD7 | `0x002` |
| 2 | DQ3 | PD6 | `0x004` |
| 3 | DQ4 | PD5 | `0x008` |
| 4 | DQ5 | PD4 | `0x010` |
| 5 | DQ6 | PD3 | `0x020` |
| 6 | DQ7 | PD2 | `0x040` |
| 7 | DQ8 | PD1 | `0x080` |
| 8 | DQ9 | PD0 | `0x100` |
| 9 | DQ10 | PC12 | `0x200` |
| 10 | DQ11 | PC11 | `0x400` |
| 11 | DQ12 | PC10 | `0x800` |

Power-on state:

- on boot all outputs are driven to the `DQ_DEFAULT_MASK` value (`HR100`, default `0` — all off)
- the value is persisted to Flash with `TRIG_SAVE` and applied on the next reboot

## STAT_LED

LED modes:

| Code | Mode |
|---:|---|
| `0` | always off |
| `1` | always on |
| `2` | state machine, default |

Patterns in state-machine mode:

| State | Behavior |
|---|---|
| No polling | 1 short blink every 3 seconds |
| Polling | 2 short blinks every 1.5 seconds |
| No link | 3 short blinks every 3 seconds |
| Factory reset | continuous blink, default `300 ms ON / 100 ms OFF` |

## Modbus TCP map

### Coils, FC01 / FC05 / FC15

| Address | Description |
|---:|---|
| `0..11` | DQ1..DQ12, output state (read/write) |

### Input Registers, FC04

| Address | Description |
|---:|---|
| `0..11` | DQ1..DQ12, values `0/1` (output-state echo) |
| `120` | firmware version major |
| `121` | firmware version minor |
| `122` | uptime seconds, low word |
| `123` | uptime seconds, high word |
| `124` | 12-bit output mask |

### Holding Registers, FC03 / FC06 / FC16

| Address | Name | Range / value | Applies |
|---:|---|---|---|
| `100` | `DQ_DEFAULT_MASK` | `0..0x0FFF`, default `0` | after save + reboot |
| `101` | `LED_MODE` | `0..2`, default `2` | immediately |
| `102` | `SLAVE_ID` | `1..247`, default `1` | for new Modbus sessions |
| `103` | `TCP_PORT` | `1..65535`, default `502` | after save + reboot |
| `104..107` | `IP_BASE` | IPv4 octets | after save + reboot |
| `108..111` | `NETMASK_BASE` | IPv4 octets | after save + reboot |
| `112..115` | `GATEWAY_BASE` | IPv4 octets | after save + reboot |
| `116` | `USE_DHCP` | `0/1`, default `1` | after save + reboot |
| `117` | `TRIG_SAVE` | write `0xA5A5` | save settings |
| `118` | `TRIG_REBOOT` | write `0xB00B` | soft reset |
| `118` | `TRIG_BOOTLOADER` | write `0xB007` | enter bootloader |
| `119` | `TRIG_FACTORY_RESET` | write `0xDEAD` | defaults + save + reset |

Invalid values return Modbus exception `ILLEGAL_DATA_VALUE`. Invalid addresses return `ILLEGAL_DATA_ADDRESS`.

## PyModbus examples

Turn DQ1 (coil 0) on and read back all 12 outputs:

```python
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient("192.168.142.147", port=502, timeout=5)
client.connect()
client.write_coil(address=0, value=True, device_id=1)            # DQ1 = ON
rr = client.read_coils(address=0, count=12, device_id=1)
print(rr.bits)
client.close()
```

Write several outputs at once (DQ1..DQ4 = ON, the rest OFF):

```python
client.write_coils(address=0, values=[True, True, True, True] + [False] * 8, device_id=1)
```

Set the power-on output mask and persist it:

```python
client.write_register(address=100, value=0x00F, device_id=1)     # DQ1..DQ4 default ON
client.write_register(address=117, value=0xA5A5, device_id=1)    # TRIG_SAVE
client.write_register(address=118, value=0xB00B, device_id=1)    # TRIG_REBOOT
```

Enter bootloader / factory reset:

```python
client.write_register(address=118, value=0xB007, device_id=1)    # bootloader
client.write_register(address=119, value=0xDEAD, device_id=1)    # factory reset
```

## Flash settings

Settings are stored in sector 10 at `0x080C0000`.

The structure is protected by:

- magic: `0x12D04A57`
- version: `1`
- CRC32 over all fields preceding `crc32`

If the Flash structure is invalid, the application loads defaults. `TRIG_SAVE` erases sector 10
and writes the current settings structure. The magic differs from 12DI (`0x12D14A57`), so 12DI
settings are not picked up by this firmware.

## Bootloader hand-over

The application and bootloader share a no-init RAM flag:

- address: `0x2001FFF0`
- magic: `0xB007CAFE`

Writing `0xB007` to `HR118` makes the application:

1. write the magic into the shared RAM cell
2. wait a short moment while refreshing the IWDG
3. call `NVIC_SystemReset()`
4. the bootloader reads the magic, clears it, and stays in `BOOT_WAIT_COMMAND`

## Known limitations

- no authentication on Modbus TCP commands
- the Modbus server serves one client at a time; additional clients wait for the connection to free up
- the current MAC address is locally-administered and must be replaced for serial production
- output pin map and polarity are inherited from 12DI and must be confirmed against the 12DQ schematic
- the firmware has not yet been verified on real 12DQ hardware

# PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6

Firmware for the PLCJS Ethernet 12-DQ module (12 discrete outputs) based on `STM32F407VGT6`.

The application drives 12 push-pull outputs (direct polarity: logical `1` = high level on the pin), accepts commands over `Modbus TCP`, handles communication loss independently per output (HOLD / ZERO / SAFE modes with a configurable timeout), stores settings in internal Flash, drives `STAT_LED`, supports factory reset, and can hand over control to the Ethernet bootloader for OTA updates.

This firmware was ported from the 12DI base variant (12 discrete inputs). The pinout and the networking/service functionality are inherited unchanged; the input driver was replaced with an output driver and the Modbus register map was reworked.

## Current status

Validated so far:

- `CMake + Ninja` build (`arm-none-eabi-gcc`, links with no errors or warnings)

Not validated yet (requires real 12DQ hardware and the Windows `starm-clang` toolchain):

- output control and communication-loss behavior on the module
- bootloader / OTA integration for the new `product_id`
- networking and service scenarios on hardware (inherited from 12DI)

## Hardware platform

| Block | Description |
|---|---|
| MCU | `STM32F407VGT6`, Cortex-M4F |
| Ethernet | `KSZ8863` Ethernet switch/PHY, RMII |
| Discrete outputs | 12 outputs, push-pull, direct polarity (active-high) |
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

In the tested network, DHCP assigned `192.168.142.98` to the application. The paired bootloader is available at `192.168.142.99` in the validated setup.

Notes:

- `USE_DHCP = 1` means the static IP is stored but not applied
- IP/port/DHCP changes take effect after `TRIG_SAVE` and `TRIG_REBOOT`
- static IP mode was tested with `192.168.142.147`

## Discrete outputs

Outputs are push-pull, direct polarity: logical `1` = high level on the pin. The polarity is inverted with a single `DQ_ACTIVE_HIGH` macro in `Application/dq/dq_module.h`.

| Modbus index | Silkscreen | MCU pin | Mask bit |
|---:|---|---|---:|
| 0 | DQ1 | PC11 | `0x001` |
| 1 | DQ2 | PC10 | `0x002` |
| 2 | DQ3 | PA9 | `0x004` |
| 3 | DQ4 | PA8 | `0x008` |
| 4 | DQ5 | PC9 | `0x010` |
| 5 | DQ6 | PC8 | `0x020` |
| 6 | DQ7 | PD10 | `0x040` |
| 7 | DQ8 | PD9 | `0x080` |
| 8 | DQ9 | PD8 | `0x100` |
| 9 | DQ10 | PE14 | `0x200` |
| 10 | DQ11 | PE13 | `0x400` |
| 11 | DQ12 | PE12 | `0x800` |

### Communication-loss modes (independent per output)

Communication loss is detected as the absence of Modbus requests for longer than the configured timeout.

| Code | Mode | Behavior after timeout |
|---:|---|---|
| `0` | `HOLD` | keep the last commanded state (default) |
| `1` | `ZERO` | force the output to `0` |
| `2` | `SAFE` | apply the configured safe value |

- the timeout is set per output in units of x100 ms (`0` = fire immediately)
- once it fires, the output is latched into safe/0 and stays there until the next explicit command for that output (no auto-restore)
- `HOLD` never changes the output automatically
- output values and the full configuration are persisted on `TRIG_SAVE` and restored at power-on

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

LED indication logic is inherited from 12DI unchanged.

## Modbus TCP map

The output control/configuration block occupies holding registers `50..98` so it never overlaps the address ranges of other module types (e.g. 12DI). Per-register addresses: channel `i` (0..11) = base address + `i`, which maps to DQ`(i+1)`.

### Input Registers, FC04

| Address | Description |
|---:|---|
| `0..11` | DQ1..DQ12, current output state `0/1` (echo, read-only) |
| `120` | firmware version major |
| `121` | firmware version minor |
| `122` | uptime seconds, low word |
| `123` | uptime seconds, high word |
| `124` | 12-bit output state mask |
| `125` | module ID = `0x12D0` (12x DO) |

### Holding Registers, FC03 / FC06 / FC16

| Address | Name | Range / value | Applied |
|---:|---|---|---|
| `50` | `DQ_GROUP` | 12-bit mask (bit `i` -> DQ`(i+1)`) | immediately |
| `51..62` | `DQ_VALUE[1..12]` | `0/1` | immediately |
| `63..74` | `DQ_MODE[1..12]` | `0`=HOLD / `1`=ZERO / `2`=SAFE, default `0` | immediately |
| `75..86` | `DQ_SAFE[1..12]` | `0/1`, default `0` | immediately |
| `87..98` | `DQ_TIMEOUT[1..12]` | x100 ms, default `0` (immediate) | immediately |
| `99` | reserved | - | - |
| `101` | `LED_MODE` | `0..2`, default `2` | immediately |
| `102` | `SLAVE_ID` | `1..247`, default `1` | new Modbus sessions |
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

### Coils, FC01 / FC05 / FC15

Coils `0..11` are an alternative interface to outputs DQ1..DQ12 (zero-based addressing):

| Coil address | Channel | R/W |
|---:|---|:---:|
| `0..11` | DQ1..DQ12 | R/W |

- a coil write (FC05/FC15) is equivalent to writing the matching `DQ_VALUE` register (`51..62`): the output is driven through the same path, so the communication-loss mode/timeout logic (`63..98`) applies identically;
- the change is applied in RAM immediately but committed to Flash only by the explicit `TRIG_SAVE` command (`117 = 0xA5A5`);
- a coil read (FC01) returns the actual output mask (same source as `DQ_MASK`, IR124).

Discrete inputs (FC02) are not implemented.

## PyModbus examples

Read firmware version, output mask and module ID:

```python
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient("192.168.142.98", port=502, timeout=5)
client.connect()
rr = client.read_input_registers(address=120, count=6, device_id=1)
print(rr.registers)  # [fw_major, fw_minor, uptime_lo, uptime_hi, dq_mask, 0x12D0]
client.close()
```

Turn DQ1 and DQ3 on (per-register), then the same via the group mask:

```python
client.write_register(address=51, value=1, device_id=1)        # DQ1 = 1
client.write_register(address=53, value=1, device_id=1)        # DQ3 = 1
client.write_register(address=50, value=0x005, device_id=1)    # group: DQ1+DQ3
```

The same via coils, plus reading the outputs back as coils:

```python
client.write_coil(address=0, value=True, device_id=1)          # DQ1 = 1 (coil 0)
client.write_coils(address=0, values=[True, False, True], device_id=1)  # DQ1..DQ3
print(client.read_coils(address=0, count=12, device_id=1).bits) # DQ1..DQ12 state
```

Configure DQ2 to go to SAFE=1 one second after a communication loss:

```python
client.write_register(address=64, value=2,  device_id=1)   # DQ2 MODE = SAFE
client.write_register(address=76, value=1,  device_id=1)   # DQ2 SAFE value = 1
client.write_register(address=88, value=10, device_id=1)   # DQ2 timeout = 10 x100ms = 1 s
```

Save settings:

```python
client.write_register(address=117, value=0xA5A5, device_id=1)
```

Reboot application:

```python
client.write_register(address=118, value=0xB00B, device_id=1)
```

Enter bootloader:

```python
client.write_register(address=118, value=0xB007, device_id=1)
```

Factory reset:

```python
client.write_register(address=119, value=0xDEAD, device_id=1)
```

## Flash settings

Settings are stored in sector 10 at `0x080C0000`.

The settings image is protected by:

- magic: `0x12D04A57` (differs from 12DI, so 12DI settings are not picked up)
- version: `1`
- CRC32 over every field before `crc32`
- includes the per-output configuration: state mask, safe mask, modes and timeouts

If the stored image is invalid, defaults are loaded. `TRIG_SAVE` erases sector 10 and writes the current settings structure.

## Bootloader handoff

The application and bootloader share a no-init RAM flag:

- address: `0x2001FFF0`
- magic: `0xB007CAFE`

When `0xB007` is written to `HR118`, the application:

1. writes the magic value to the shared RAM cell
2. waits briefly while refreshing IWDG
3. calls `NVIC_SystemReset()`
4. the bootloader consumes the magic, clears it, and stays in `BOOT_WAIT_COMMAND`

## Recovery and operation notes

- after `factory reset`, defaults are saved and the device reboots
- after IP/port/DHCP changes, always use `TRIG_SAVE` and `TRIG_REBOOT`
- entering the bootloader moves the device from app IP to bootloader IP
- if the bootloader remains active after an interrupted OTA session, use `ABORT_UPDATE` and then `REBOOT` on the bootloader side

## Validation status

| Test | Result |
|---|---|
| Build (`arm-none-eabi-gcc`, CMake + Ninja) | OK, no warnings |
| DQ1..DQ12 control on hardware | not validated |
| HOLD/ZERO/SAFE modes and timeouts on hardware | not validated |
| OTA / bootloader for the new `product_id` | not validated |
| Networking/service scenarios (inherited from 12DI) | not validated on 12DQ |

## Known limitations

- no authentication for Modbus TCP commands
- the Modbus server serves one client at a time; additional clients wait until the connection is released
- the current MAC address is locally-administered and should be replaced for production
- bootloader IP is configured in the separate bootloader project and is static in the current validated setup

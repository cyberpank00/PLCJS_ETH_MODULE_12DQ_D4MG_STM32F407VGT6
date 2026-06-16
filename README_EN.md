# PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6

Main firmware for the PLCJS Ethernet 12-DI module based on `STM32F407VGT6`.

The application reads 12 active-low discrete inputs, applies debounce filtering, exposes state and configuration over `Modbus TCP`, stores settings in internal Flash, drives `STAT_LED`, supports factory reset, and can hand over control to the Ethernet bootloader for OTA updates.

## Current status

The application is integrated with the paired bootloader project and tested on real hardware.

Validated scenarios:

- `CMake + Ninja` build
- application boot from bootloader
- `ping` and `Modbus TCP` access on the running application
- Flash settings persistence and reboot application
- `DHCP <-> static IP` switching
- `TCP_PORT 502 <-> 1502` switching
- `reboot`, `factory reset`, `enter bootloader` commands
- all inputs `DI1..DI12`
- LED patterns: `idle`, `polling`, `no link`, `factory reset`
- negative Modbus scenarios
- polling / reconnect / ping soak tests

## Hardware platform

| Block | Description |
|---|---|
| MCU | `STM32F407VGT6`, Cortex-M4F |
| Ethernet | `KSZ8863` Ethernet switch/PHY, RMII |
| Discrete inputs | 12 inputs, active-low, MCU internal pull-ups |
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

- `build/Debug/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.elf`
- `build/Debug/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.hex`
- `build/Debug/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.bin`
- `build/Debug/app.bin` - OTA image for the bootloader

## Flashing

Via ST-Link:

```powershell
STM32_Programmer_CLI -c port=SWD -w build/Debug/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.elf -v -rst
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

## Discrete inputs

Inputs are active-low: shorting an input to GND publishes logical `1`.

| Modbus index | Silkscreen | MCU pin | Mask bit |
|---:|---|---|---:|
| 0 | DI1 | PB3 | `0x001` |
| 1 | DI2 | PD7 | `0x002` |
| 2 | DI3 | PD6 | `0x004` |
| 3 | DI4 | PD5 | `0x008` |
| 4 | DI5 | PD4 | `0x010` |
| 5 | DI6 | PD3 | `0x020` |
| 6 | DI7 | PD2 | `0x040` |
| 7 | DI8 | PD1 | `0x080` |
| 8 | DI9 | PD0 | `0x100` |
| 9 | DI10 | PC12 | `0x200` |
| 10 | DI11 | PC11 | `0x400` |
| 11 | DI12 | PC10 | `0x800` |

Input filter:

- sampling period: `1 ms`
- default: `50 ms`
- range: `10..1000 ms`
- writing `HR100` applies immediately
- Flash persistence requires `TRIG_SAVE`

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

The `idle -> polling -> idle` and `no link` patterns were visually verified on the module.

## Modbus TCP map

### Discrete Inputs, FC02

| Address | Description |
|---:|---|
| `0..11` | DI1..DI12, filtered state |

### Input Registers, FC04

| Address | Description |
|---:|---|
| `0..11` | DI1..DI12, values `0/1` |
| `120` | firmware version major |
| `121` | firmware version minor |
| `122` | uptime seconds, low word |
| `123` | uptime seconds, high word |
| `124` | 12-bit DI mask |

### Holding Registers, FC03 / FC06 / FC16

| Address | Name | Range / value | Applied |
|---:|---|---|---|
| `100` | `DI_FILTER_MS` | `10..1000`, default `50` | immediately |
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

## PyModbus examples

Read firmware version and DI mask:

```python
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient("192.168.142.98", port=502, timeout=5)
client.connect()
rr = client.read_input_registers(address=120, count=5, device_id=1)
print(rr.registers)
client.close()
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

- magic: `0x12D14A57`
- version: `1`
- CRC32 over every field before `crc32`

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

## Validated test set

| Test | Result |
|---|---|
| Build app | OK |
| Ping `.98` | OK |
| Modbus read/write | OK |
| Reconnect 20/20 | OK |
| Polling 100/100 | OK |
| Soak ping 120/120 | OK, 0% loss |
| Soak Modbus 240/240 | OK |
| `DI1..DI12` | OK |
| `TCP_PORT 502 <-> 1502` | OK |
| `DHCP <-> static .147` | OK |
| `reboot` | OK |
| `factory reset` | OK |
| `app -> bootloader -> app` | OK |
| Negative Modbus values | OK |
| LED idle/polling/no-link | OK |

## Known limitations

- no authentication for Modbus TCP commands
- the Modbus server serves one client at a time; additional clients wait until the connection is released
- the current MAC address is locally-administered and should be replaced for production
- bootloader IP is configured in the separate bootloader project and is static in the current validated setup

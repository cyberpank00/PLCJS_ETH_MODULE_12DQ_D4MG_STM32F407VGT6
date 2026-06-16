# PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6

Основная прошивка Ethernet-модуля PLCJS с 12 дискретными выходами на базе `STM32F407VGT6`.

Прошивка управляет 12 дискретными выходами по `Modbus TCP` (Modbus coils), хранит настройки
во внутренней Flash, управляет индикатором `STAT_LED`, поддерживает factory reset и программный
переход в Ethernet bootloader для OTA-обновления.

## Происхождение

Порт прошивки [`PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6`](https://github.com/cyberpank00/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6):
12 дискретных **входов** заменены на 12 дискретных **выходов**. Вся сетевая инфраструктура
(KSZ8863/RMII, LwIP, Modbus TCP server, настройки во Flash, STAT_LED, FACT_RES, переход в
bootloader) сохранена без изменений.

## Текущий статус

- сборка `CMake + Ninja` (Linux syntax-check `arm-none-eabi-gcc`, целевая сборка `starm-clang`)
- **прошивка ещё не валидирована на реальном модуле 12DQ** — требуется проверка распиновки и
  полярности выходов по схеме платы 12DQ (см. раздел «Дискретные выходы»)

## Аппаратная платформа

| Узел | Описание |
|---|---|
| MCU | `STM32F407VGT6`, Cortex-M4F |
| Ethernet | `KSZ8863` Ethernet switch/PHY, RMII |
| Дискретные выходы | 12 выходов, push-pull, полярность настраивается (`DQ_ACTIVE_HIGH`) |
| Factory reset | кнопка `FACT_RES`, active-low |
| Индикация | `STAT_LED`, active-high |
| Watchdog | `IWDG`, обновляется из основного цикла приложения |

## Flash и RAM layout

Приложение собрано как image для работы вместе с bootloader.

| Область | Адрес | Размер | Назначение |
|---|---:|---:|---|
| Bootloader | `0x08000000` | 128 KB | sectors 0-4, другой проект |
| Metadata | `0x08020000` | 128 KB | sector 5, состояние OTA |
| Application | `0x08040000` | 256 KB | sectors 6-7, эта прошивка |
| Staging | `0x08080000` | 256 KB | sectors 8-9, OTA staging |
| Settings | `0x080C0000` | 128 KB | sector 10, настройки приложения |

RAM:

- основной RAM начинается с `0x20000000`
- верхние 16 байт зарезервированы под shared boot-request flag
- shared flag address: `0x2001FFF0`
- shared flag magic: `0xB007CAFE`

## Сборка

Требуется STM32CubeCLT с `starm-clang`, `cmake` и `ninja`.

```powershell
cmake -S . -B build/Debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake
cmake --build build/Debug
```

Основные результаты сборки:

- `build/Debug/PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6.elf`
- `build/Debug/PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6.hex`
- `build/Debug/PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6.bin`
- `build/Debug/app.bin` - OTA image для bootloader

## Прошивка

Через ST-Link:

```powershell
STM32_Programmer_CLI -c port=SWD -w build/Debug/PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6.elf -v -rst
```

Через bootloader OTA используется `app.bin` из `build/Debug` и клиент `tools/fw_update.py` из bootloader-репозитория.

## Сеть

Настройки сети хранятся во Flash и доступны через Modbus holding registers.

Дефолты:

| Параметр | Значение |
|---|---|
| DHCP | `1` / включен |
| Static IP | `192.168.142.147` |
| Netmask | `255.255.255.0` |
| Gateway | `192.168.142.1` |
| Modbus TCP port | `502` |
| Modbus unit id | `1` |

Важно:

- `USE_DHCP = 1` означает, что static IP хранится как запасная настройка, но не применяется
- изменения IP/port/DHCP вступают в силу после `TRIG_SAVE` и `TRIG_REBOOT`

## Дискретные выходы

Выходы push-pull. Логическая `1` по умолчанию выставляет высокий уровень на пине MCU
(`DQ_ACTIVE_HIGH = 1` в `Application/dq/dq_module.h`); инвертировать полярность можно одним
макросом.

> Распиновка ниже унаследована от платы 12DI (те же пины переведены в режим выхода).
> Перед прошивкой на реальный модуль 12DQ сверьте таблицу со схемой платы.

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

Состояние после включения:

- на старте все выходы выставляются в маску `DQ_DEFAULT_MASK` (`HR100`, дефолт `0` — все выключены)
- значение сохраняется во Flash по `TRIG_SAVE` и применяется при следующем reboot

## STAT_LED

Режимы LED:

| Код | Режим |
|---:|---|
| `0` | всегда выключен |
| `1` | всегда включен |
| `2` | state machine, дефолт |

Паттерны в режиме state machine:

| Состояние | Поведение |
|---|---|
| No polling | 1 короткая вспышка каждые 3 секунды |
| Polling | 2 короткие вспышки каждые 1.5 секунды |
| No link | 3 короткие вспышки каждые 3 секунды |
| Factory reset | непрерывное мигание, дефолт `300 ms ON / 100 ms OFF` |

## Modbus TCP карта

### Coils, FC01 / FC05 / FC15

| Address | Описание |
|---:|---|
| `0..11` | DQ1..DQ12, состояние выхода (чтение/запись) |

### Input Registers, FC04

| Address | Описание |
|---:|---|
| `0..11` | DQ1..DQ12, значения `0/1` (эхо состояния выходов) |
| `120` | firmware version major |
| `121` | firmware version minor |
| `122` | uptime seconds, low word |
| `123` | uptime seconds, high word |
| `124` | 12-битная маска выходов |

### Holding Registers, FC03 / FC06 / FC16

| Address | Имя | Диапазон / значение | Применение |
|---:|---|---|---|
| `100` | `DQ_DEFAULT_MASK` | `0..0x0FFF`, default `0` | после save + reboot |
| `101` | `LED_MODE` | `0..2`, default `2` | сразу |
| `102` | `SLAVE_ID` | `1..247`, default `1` | для новых Modbus-сессий |
| `103` | `TCP_PORT` | `1..65535`, default `502` | после save + reboot |
| `104..107` | `IP_BASE` | IPv4 octets | после save + reboot |
| `108..111` | `NETMASK_BASE` | IPv4 octets | после save + reboot |
| `112..115` | `GATEWAY_BASE` | IPv4 octets | после save + reboot |
| `116` | `USE_DHCP` | `0/1`, default `1` | после save + reboot |
| `117` | `TRIG_SAVE` | write `0xA5A5` | сохранить настройки |
| `118` | `TRIG_REBOOT` | write `0xB00B` | soft reset |
| `118` | `TRIG_BOOTLOADER` | write `0xB007` | перейти в bootloader |
| `119` | `TRIG_FACTORY_RESET` | write `0xDEAD` | defaults + save + reset |

Некорректные значения возвращают Modbus exception `ILLEGAL_DATA_VALUE`. Некорректные адреса возвращают `ILLEGAL_DATA_ADDRESS`.

## Примеры PyModbus

Включить выход DQ1 (coil 0) и прочитать все 12 выходов:

```python
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient("192.168.142.147", port=502, timeout=5)
client.connect()
client.write_coil(address=0, value=True, device_id=1)            # DQ1 = ON
rr = client.read_coils(address=0, count=12, device_id=1)
print(rr.bits)
client.close()
```

Записать сразу несколько выходов (DQ1..DQ4 = ON, остальные OFF):

```python
client.write_coils(address=0, values=[True, True, True, True] + [False] * 8, device_id=1)
```

Прочитать версию и маску выходов:

```python
rr = client.read_input_registers(address=120, count=5, device_id=1)
print(rr.registers)
```

Задать маску выходов после reboot и сохранить:

```python
client.write_register(address=100, value=0x00F, device_id=1)     # DQ1..DQ4 по умолчанию ON
client.write_register(address=117, value=0xA5A5, device_id=1)    # TRIG_SAVE
client.write_register(address=118, value=0xB00B, device_id=1)    # TRIG_REBOOT
```

Перейти в bootloader / factory reset:

```python
client.write_register(address=118, value=0xB007, device_id=1)    # bootloader
client.write_register(address=119, value=0xDEAD, device_id=1)    # factory reset
```

## Настройки во Flash

Настройки хранятся в sector 10 по адресу `0x080C0000`.

Структура защищена:

- magic: `0x12D04A57`
- version: `1`
- CRC32 по всем полям до `crc32`

Если структура во Flash невалидна, приложение загружает дефолты. `TRIG_SAVE` стирает sector 10 и записывает актуальную структуру настроек. Magic отличается от 12DI (`0x12D14A57`), поэтому настройки от прошивки 12DI не подхватываются.

## Переход в bootloader

Приложение и bootloader используют shared no-init RAM flag:

- address: `0x2001FFF0`
- magic: `0xB007CAFE`

При записи `0xB007` в `HR118` приложение:

1. записывает magic в shared RAM cell
2. ждет короткую паузу, обновляя IWDG
3. выполняет `NVIC_SystemReset()`
4. bootloader считывает magic, очищает его и остается в `BOOT_WAIT_COMMAND`

## Известные ограничения

- нет аутентификации Modbus TCP команд
- Modbus server обслуживает одного клиента за раз; дополнительные клиенты ждут освобождения соединения
- текущий MAC address является locally-administered и должен быть заменен для серийного производства
- распиновка и полярность выходов унаследованы от 12DI и должны быть подтверждены по схеме 12DQ
- прошивка ещё не проверена на реальном железе 12DQ

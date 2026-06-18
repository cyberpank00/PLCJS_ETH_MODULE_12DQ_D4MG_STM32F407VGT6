# PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6

Прошивка Ethernet-модуля PLCJS с 12 дискретными выходами на базе `STM32F407VGT6`.

Прошивка управляет 12 push-pull выходами (прямая полярность: логическая `1` = высокий уровень на выводе), принимает команды по `Modbus TCP`, индивидуально для каждого выхода отрабатывает потерю связи (режимы HOLD / ZERO / SAFE с настраиваемым таймаутом), хранит настройки во внутренней Flash, управляет индикатором `STAT_LED`, поддерживает factory reset и программный переход в Ethernet bootloader для OTA-обновления.

Прошивка получена портированием базового варианта 12DI (12 дискретных входов). Распиновка и сетевой/служебный функционал унаследованы без изменений; драйвер входов заменён на драйвер выходов, переработана карта Modbus-регистров.

## Текущий статус

Проверено на текущем этапе:

- сборка `CMake + Ninja` (`arm-none-eabi-gcc`, линковка без ошибок и предупреждений)

Не проверялось (требуется реальное железо 12DQ и Windows-тулчейн `starm-clang`):

- управление выходами и поведение при потере связи на модуле
- интеграция с bootloader / OTA для нового `product_id`
- сетевые и служебные сценарии на железе (унаследованы от 12DI)

## Аппаратная платформа

| Узел | Описание |
|---|---|
| MCU | `STM32F407VGT6`, Cortex-M4F |
| Ethernet | `KSZ8863` Ethernet switch/PHY, RMII |
| Дискретные выходы | 12 выходов, push-pull, прямая полярность (active-high) |
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

Требуется STM32CubeCLT с `starm-clang`, `cmake`, `ninja` и `python3`.

Стандартная сборка для варианта 12-DQ/D4MG (`PRODUCT_ID=0x12D0D4A0`, `HW_REVISION=1`):

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
- `build/Debug/app.bin` — OTA image для bootloader, с встроенным `fw_header_t`

Post-build шаг автоматически запускает `tools/gen_app_bin.py`, который:

1. берёт сырой `.bin`
2. проверяет, что по смещению `0x200` находится корректный `fw_header_t` (magic `"PLCJ"`)
3. заполняет поля `image_size` и `image_crc32` (CRC32 с полем CRC = 0)
4. сохраняет результат как `app.bin`

Итоговый `app.bin` содержит заголовок с `product_id`, `hw_revision` и `fw_version`,
которые бутлоадер проверяет из самого бинарника при OTA-обновлении.

## Многовариантная сборка

Прошивка поддерживает сборку для разных вариантов модуля через CMake-параметры.
При каждой сборке в бинарник вшиваются корректные `product_id` и `hw_revision`,
а бутлоадер, скомпилированный для того же варианта, примет **только** этот образ.

Доступные параметры:

| Параметр CMake | Значение по умолчанию | Описание |
|---|---|---|
| `-DPRODUCT_ID=0x...` | `0x12D0D4A0` | Идентификатор варианта модуля |
| `-DHW_REVISION=N` | `1` | Ревизия платы |
| `-DFW_VERSION=0xMMmmpp` | `0x00010000` | Версия прошивки (major/minor/patch) |

Примеры для разных вариантов:

```powershell
# 12 дискретных выходов — вариант по умолчанию (эта прошивка)
cmake -S . -B build/12dq -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake `
  -DPRODUCT_ID=0x12D0D4A0 -DHW_REVISION=1 -DFW_VERSION=0x00010000

# 12 дискретных входов
cmake -S . -B build/12di -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake `
  -DPRODUCT_ID=0x12D1D4A0 -DHW_REVISION=1

# 4 входа RTD
cmake -S . -B build/4rtd -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake `
  -DPRODUCT_ID=0x04D00000 -DHW_REVISION=1

# 8 аналоговых входов
cmake -S . -B build/8ai -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake `
  -DPRODUCT_ID=0x08A10000 -DHW_REVISION=1

# 8 аналоговых выходов
cmake -S . -B build/8ao -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake `
  -DPRODUCT_ID=0x08A00000 -DHW_REVISION=1
```

После сборки `app.bin` в папке варианта несёт правильный `product_id`, и бутлоадер
для **другого** варианта отклонит его при `FINALIZE_UPDATE`.

### Как это работает

```
Сборка → fw_header.c компилируется с -DFW_PRODUCT_ID=0x12D0D4A0
                                       -DFW_HW_REVISION=1
                                       -DFW_VERSION_VALUE=0x00010000
       → линкер помещает g_fw_header в секцию .fw_header по смещению 0x200
       → gen_app_bin.py заполняет image_size и image_crc32
       → app.bin готов

OTA-обновление → бутлоадер читает product_id/hw_revision из бинарника
               → при несоответствии: PRODUCT_MISMATCH, BOOT_ERROR
               → при совпадении: образ устанавливается
```

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

В тестовой сети DHCP выдавал приложению адрес `192.168.142.98`. Bootloader в протестированной конфигурации доступен на `192.168.142.99`.

Важно:

- `USE_DHCP = 1` означает, что static IP хранится как запасная настройка, но не применяется
- изменения IP/port/DHCP вступают в силу после `TRIG_SAVE` и `TRIG_REBOOT`
- static IP mode был проверен на `192.168.142.147`

## Дискретные выходы

Выходы push-pull, прямая полярность: логическая `1` = высокий уровень на выводе. Полярность инвертируется одним макросом `DQ_ACTIVE_HIGH` в `Application/dq/dq_module.h`.

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

### Режимы при потере связи (индивидуально на каждый выход)

Потеря связи определяется по отсутствию Modbus-запросов дольше заданного таймаута.

| Код | Режим | Поведение после таймаута |
|---:|---|---|
| `0` | `HOLD` | удерживать последнее состояние (дефолт) |
| `1` | `ZERO` | сбросить выход в `0` |
| `2` | `SAFE` | установить настроенное безопасное значение |

- таймаут задаётся на каждый выход в единицах ×100 мс (`0` = сработать сразу)
- после срабатывания выход защёлкивается в safe/0 и остаётся в этом состоянии до следующей явной команды на этот выход (автовосстановления нет)
- режим `HOLD` никогда не меняет выход автоматически
- значения выходов и вся конфигурация сохраняются по `TRIG_SAVE` и восстанавливаются при включении

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

Логика индикации унаследована от 12DI без изменений.

## Modbus TCP карта

Блок управления/конфигурации выходов занимает holding-регистры `50..98`, чтобы не пересекаться с адресами других типов модулей (например, 12DI). Адреса «порегистрово» — канал `i` (0..11) = базовый адрес + `i`, что соответствует DQ`(i+1)`.

### Input Registers, FC04

| Address | Описание |
|---:|---|
| `0..11` | DQ1..DQ12, текущее состояние выхода `0/1` (эхо, read-only) |
| `120` | firmware version major |
| `121` | firmware version minor |
| `122` | uptime seconds, low word |
| `123` | uptime seconds, high word |
| `124` | 12-bit маска состояния выходов |
| `125` | module ID = `0x12D0` (12x DO) |

### Holding Registers, FC03 / FC06 / FC16

| Address | Имя | Диапазон / значение | Применение |
|---:|---|---|---|
| `50` | `DQ_GROUP` | 12-битная маска (бит `i` → DQ`(i+1)`) | сразу |
| `51..62` | `DQ_VALUE[1..12]` | `0/1` | сразу |
| `63..74` | `DQ_MODE[1..12]` | `0`=HOLD / `1`=ZERO / `2`=SAFE, default `0` | сразу |
| `75..86` | `DQ_SAFE[1..12]` | `0/1`, default `0` | сразу |
| `87..98` | `DQ_TIMEOUT[1..12]` | ×100 мс, default `0` (сразу) | сразу |
| `99` | резерв | — | — |
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

Чтение версии, маски выходов и module ID:

```python
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient("192.168.142.98", port=502, timeout=5)
client.connect()
rr = client.read_input_registers(address=120, count=6, device_id=1)
print(rr.registers)  # [fw_major, fw_minor, uptime_lo, uptime_hi, dq_mask, 0x12D0]
client.close()
```

Включить DQ1 и DQ3 (порегистрово) и всё групповой маской:

```python
client.write_register(address=51, value=1, device_id=1)        # DQ1 = 1
client.write_register(address=53, value=1, device_id=1)        # DQ3 = 1
client.write_register(address=50, value=0x005, device_id=1)    # группа: DQ1+DQ3
```

Настроить DQ2: при потере связи через 1 с перейти в SAFE=1:

```python
client.write_register(address=64, value=2,  device_id=1)   # DQ2 MODE = SAFE
client.write_register(address=76, value=1,  device_id=1)   # DQ2 SAFE value = 1
client.write_register(address=88, value=10, device_id=1)   # DQ2 timeout = 10 x100ms = 1 s
```

Сохранить настройки:

```python
client.write_register(address=117, value=0xA5A5, device_id=1)
```

Перезагрузить приложение:

```python
client.write_register(address=118, value=0xB00B, device_id=1)
```

Перейти в bootloader:

```python
client.write_register(address=118, value=0xB007, device_id=1)
```

Factory reset:

```python
client.write_register(address=119, value=0xDEAD, device_id=1)
```

## Настройки во Flash

Настройки хранятся в sector 10 по адресу `0x080C0000`.

Структура защищена:

- magic: `0x12D04A57` (отличается от 12DI — настройки 12DI не подхватятся)
- version: `1`
- CRC32 по всем полям до `crc32`
- включает повыходную конфигурацию: маску состояния, safe-маску, режимы и таймауты

Если структура во Flash невалидна, приложение загружает дефолты. `TRIG_SAVE` стирает sector 10 и записывает актуальную структуру настроек.

## Переход в bootloader

Приложение и bootloader используют shared no-init RAM flag:

- address: `0x2001FFF0`
- magic: `0xB007CAFE`

При записи `0xB007` в `HR118` приложение:

1. записывает magic в shared RAM cell
2. ждет короткую паузу, обновляя IWDG
3. выполняет `NVIC_SystemReset()`
4. bootloader считывает magic, очищает его и остается в `BOOT_WAIT_COMMAND`

## Recovery и эксплуатационные заметки

- после `factory reset` настройки возвращаются к дефолтам и устройство перезагружается
- после смены IP/port/DHCP обязательно делайте `TRIG_SAVE` и `TRIG_REBOOT`
- при переходе в bootloader приложение уходит с app IP, bootloader поднимается на bootloader IP
- если bootloader остался после неудачной OTA-сессии, используйте `ABORT_UPDATE`, затем `REBOOT` на стороне bootloader

## Статус проверки

| Тест | Результат |
|---|---|
| Build (`arm-none-eabi-gcc`, CMake + Ninja) | OK, без предупреждений |
| Управление DQ1..DQ12 на железе | не проверялось |
| Режимы HOLD/ZERO/SAFE и таймауты на железе | не проверялось |
| OTA / bootloader для нового `product_id` | не проверялось |
| Сетевые/служебные сценарии (унаследованы от 12DI) | не проверялось на 12DQ |

## Известные ограничения

- нет аутентификации Modbus TCP команд
- Modbus server обслуживает одного клиента за раз; дополнительные клиенты ждут освобождения соединения
- текущий MAC address является locally-administered и должен быть заменен для серийного производства
- bootloader IP настраивается в отдельном проекте и сейчас статический
